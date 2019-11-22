/*
 *
 * Copyright (C) 2008-2009 Palm, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * [NOTICE]
 * modified by Baik - Samsung
 * this module must examine about license.
 * If SAMSUNG have a problem with the license, we must re-program module for
 * low memory notification.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/hugetlb.h>

#include <linux/sched.h>

#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/shmem_fs.h>
#include <linux/delay.h>
#include <asm/atomic.h>

#include <linux/slp_lowmem_notify.h>

#define MB(pages) ((K(pages))>>10)
#define K(pages) ((pages) << (PAGE_SHIFT - 10))

struct memnotify_file_info {
	int last_threshold;
	struct file *file;
	unsigned int nr_events;
};

static DECLARE_WAIT_QUEUE_HEAD(memnotify_wait);

static int b_drv_init = 0;

static int b_enable= 1;

static atomic_t nr_watcher_task = ATOMIC_INIT(0);

#ifdef CONFIG_KPI_SYSTEM_SUPPORT
    extern void set_kpi_fault(unsigned long pc, unsigned long lr, char *thread_name, char *process_name);
#endif

#ifdef CONFIG_SAVELOGD
    extern ssize_t save_write2(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);
#endif

static int s_log_level= LOG_LV_INFO;
#define LOG_PRINT(lvl, args...) { \
    do { \
        if( lvl<=s_log_level ) __LOG_PRINT(lvl, args); \
    } while(0); \
}


#define VICTIM_SCHEDULED_MS         1


#define DEFAULT_THRESHOLD_MARGIN        5      //MB
static size_t threshold_margin_mb= DEFAULT_THRESHOLD_MARGIN;


#define ENTER_MEM_LOW               40      //MB
#define ENTER_MEM_CRITICAL          35      //MB
#define ENTER_MEM_DEADLY            30      //MB

enum {
	THRESHOLD_NORMAL= 0,
	THRESHOLD_LOW,
	THRESHOLD_CRITICAL,
	THRESHOLD_DEADLY,
};
#define NR_MEMNOTIFY_LEVEL          (THRESHOLD_DEADLY+1)

static const char *_threshold_string[NR_MEMNOTIFY_LEVEL] = {
	"normal  ",
	"low     ",
	"critical",
	"deadly  ",
};

static const char *threshold_string(int threshold)
{
	if (threshold >= NR_MEMNOTIFY_LEVEL)
		return _threshold_string[NR_MEMNOTIFY_LEVEL];

	return _threshold_string[threshold];
}

static unsigned long memnotify_messages[NR_MEMNOTIFY_LEVEL] = {
	MEMNOTIFY_NORMAL,	/* The happy state */
	MEMNOTIFY_LOW,		/* Userspace drops uneeded memory */
	MEMNOTIFY_CRITICAL,	/* Userspace OOM Killer */
	MEMNOTIFY_DEADLY,	/* Critical point, kill victims by kernel */
};


static atomic_t memnotify_last_threshold = ATOMIC_INIT(THRESHOLD_NORMAL);

static size_t memnotify_enter_thresholds_mb[NR_MEMNOTIFY_LEVEL] = {
	INT_MAX,
	ENTER_MEM_LOW,
	ENTER_MEM_CRITICAL,
	ENTER_MEM_DEADLY,
};

static size_t memnotify_leave_threshold_mb= ENTER_MEM_LOW+DEFAULT_THRESHOLD_MARGIN;



static inline int adj2score(int oom_adj)
{
    int oom_score_adj;
    
    if (oom_adj >= 0) {
        oom_score_adj = ( (oom_adj*OOM_SCORE_ADJ_MAX) + (OOM_ADJUST_MAX-1) ) / OOM_ADJUST_MAX;
    }
    else {
        oom_score_adj = ( (oom_adj*OOM_SCORE_ADJ_MAX) - ((-OOM_DISABLE)-1) ) / (-OOM_DISABLE);
    }
    
    return oom_score_adj;
}

static inline int score2adj(int oom_score_adj)
{
    int oom_adj;
    
    if (oom_score_adj >= 0) {
        oom_adj = (oom_score_adj * OOM_ADJUST_MAX) / OOM_SCORE_ADJ_MAX;
    }
    else {
        oom_adj = (oom_score_adj * (-OOM_DISABLE)) / OOM_SCORE_ADJ_MAX;
    }
    
    return oom_adj;
}


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


static inline unsigned long memnotify_get_used(void)
{
	return totalram_pages - memnotify_get_reclaimable() -
	    memnotify_get_free();
}

static inline unsigned long memnotify_get_total(void)
{
	return totalram_pages;
}


#define OOM_TYPE_UNK	-1
#define OOM_TYPE_BI	    0	// background inactive 
#define OOM_TYPE_BA	    1	// background active 
#define OOM_TYPE_F	    2	// foreground
#define OOM_TYPE_FS	    3	// foreground special
#define OOM_TYPE_SYS    4	// sys daemon 
#define OOM_TYPE_VIP    5   // vip process
#define OOM_TYPE_OTHER  6	// other cases
#define OOM_TYPES       (OOM_TYPE_OTHER+1)

#define VIP_OOM_ADJ     (-15)

static int oom_adj_to_type(int oom_adj)
{
	if (4<=oom_adj && oom_adj<=OOM_ADJUST_MAX)   //background aged
		return OOM_TYPE_BI;
		
	if (oom_adj == 3)   //app service(background active)
		return OOM_TYPE_BA;
		
	if (oom_adj == 1)   //foregournd and widget
		return OOM_TYPE_F;
		
	if (oom_adj == 0)   //menu-screen...
		return OOM_TYPE_FS;
		
	if( oom_adj == VIP_OOM_ADJ ) //vip
	    return OOM_TYPE_VIP;
	    
	if (oom_adj == OOM_DISABLE) //system daemon
		return OOM_TYPE_SYS;
		
	return OOM_TYPE_OTHER;
}


static void print_stat(unsigned int *stat, int log_level)
{
    LOG_PRINT(log_level, "Free:%luMB, Reclaim:%luMB, Swap:%luMB\n"
    	"BI(4~15):%u, BA(3):%u, F(1):%u, FS(0):%u, VIP(-15):%u, SYS(-17):%u, Unknown:%u\n",
        MB(memnotify_get_free()),
        MB(memnotify_get_reclaimable()),
        MB(get_nr_swap_pages()),
        stat[OOM_TYPE_BI],
        stat[OOM_TYPE_BA],
        stat[OOM_TYPE_F],
        stat[OOM_TYPE_FS],
        stat[OOM_TYPE_VIP],
        stat[OOM_TYPE_SYS],
        stat[OOM_TYPE_OTHER]
    );
}


/**
 * update_oom_stat() - helper function to update OOM statistics
 * @stat:     massive of types, sized OOM_TYPES
 * @oom_type: type of application OOM_TYPE_*
 */
static void update_oom_stat(unsigned int *stat, int oom_type)
{
	stat[oom_type]++;
}


static struct task_rss_t {
	pid_t pid;
	char comm[TASK_COMM_LEN];
	long rss;
} task_rss[256];		// array for tasks and rss info 
static int nr_tasks= 0;		// number of tasks to be stored 
static int check_peak= 0;		// flag to store the rss or not 
static long min_real_free= 0;	// last minimum of free memory size 


static void save_task_rss(void)
{
	struct task_struct *p, *tsk;
	int i;

	nr_tasks = 0;
	rcu_read_lock();
	for_each_process(tsk) {
		if (tsk->flags & PF_KTHREAD)
			continue;

		if (!pid_alive(tsk))
			continue;

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		task_rss[nr_tasks].pid = p->pid;
		strlcpy(task_rss[nr_tasks].comm, p->comm,
			sizeof(task_rss[nr_tasks].comm));
		task_rss[nr_tasks].rss = 0;

		/* If except pages in swap, use get_mm_rss() */
		for (i = 0; i < NR_MM_COUNTERS; i++)
			task_rss[nr_tasks].rss +=
			p->mm->rss_stat.count[i].counter;

		if ((++nr_tasks) > ARRAY_SIZE(task_rss)) {
			task_unlock(p);
			break;
		}
		task_unlock(p);
	}
	rcu_read_unlock();
}

static void memnotify_wakeup(int threshold)
{
	wake_up_interruptible_all(&memnotify_wait);
}

/* dump_tasks func to dump current memory state of all system tasks. */
extern void dump_tasks(const struct mem_cgroup *mem,
		    const nodemask_t *nodemask, struct seq_file *s);

/* static variable to dump_tasks info only once. */
int fault_task_only_once;
static struct mutex victim_lock;
static unsigned long lowmem_deathpending_timeout= 0;


static inline void victim_scheduled(gfp_t gfp_mask)
{
    //if current process will be death with TIF_MEMDIE, 
    //the process don't need to sleep to get free memory.
    
    //if not, we make chance to be killed vicitim.
    
    if ( !test_thread_flag(TIF_MEMDIE) && (gfp_mask & __GFP_WAIT) ) {
        schedule_timeout_killable(VICTIM_SCHEDULED_MS);
    }//if
}    


static inline long get_total_free_mb(void)
{
    long real_free, reclaimable;
    
    real_free = MB(memnotify_get_free());
	reclaimable = MB(memnotify_get_reclaimable());
	
	return (real_free+reclaimable);
}


/*
 LMN: selected -> set MEMDIE of selected -> 
 KERNEL: detach selected from process list -> 
 KERNEL: reclaimming (LMN cannot check this phase) -> 
 KERNEL: update free memory info.
*/



static int check_threshold(size_t threshold)
{
    long real_free= MB(memnotify_get_free());
    long reclaimable = MB(memnotify_get_reclaimable());    
    
    
#if(1)  //TODO
    return ( (real_free + reclaimable)<threshold );
#else
    return ( real_free<threshold && reclaimable<threshold );
#endif    
}


//TODO
static int memnotify_victim_task(int cur_pid, const char *cur_comm, int force_kill, gfp_t gfp_mask)
{
	struct task_struct *p, *tsk;
	struct task_struct *selected = NULL;
	int tasksize;
	int selected_tasksize = 0;
	int oom_adj, selected_oom_adj = OOM_DISABLE;
	int ret_pid = 0;
	int total_selected_tasksize_mb = 0;	
	unsigned int oom_type_stat[OOM_TYPES] = { 0 };
#ifdef CONFIG_SLP_LOWMEM_NOTIFY_PRELOAD
	unsigned int preload_prio, selected_preload_prio = 0;
#endif
    long sum_of_rss_mb= 0;
    int selected_pid;
    char selected_comm[TASK_COMM_LEN];


	/* lock current tasklist state */
	rcu_read_lock();
	for_each_process(tsk) {
		if (is_global_init(tsk))
			continue;

		if (tsk->flags & PF_KTHREAD)
			continue;

		if (!pid_alive(tsk))
			continue;

		p = find_lock_task_mm(tsk);
		if (!p) {
			continue;
		}//if
			
		tasksize = get_mm_rss(p->mm);
		if( tasksize<0 ) tasksize= 0;
		oom_adj = score2adj(p->signal->oom_score_adj);
		update_oom_stat(oom_type_stat, oom_adj_to_type(oom_adj));

		if (oom_adj == OOM_DISABLE)
			goto next_task;

#ifdef CONFIG_SLP_LOWMEM_NOTIFY_VIPAPP
        if ( (p->signal->vip_app) && (oom_adj!=VIP_OOM_ADJ) ) {
            p->signal->oom_score_adj= adj2score(VIP_OOM_ADJ);
        }//if
#endif

        //we make chance to take time to reclaim after selecting the first victim.
		if( test_tsk_thread_flag(p, TIF_MEMDIE) &&
		    time_before_eq(jiffies, lowmem_deathpending_timeout) ) {

			task_unlock(p);
			rcu_read_unlock();

            LOG_PRINT(LOG_LV_TRACE, "[Lv3][LEAVE2] cur_pid=%d(%s), free:%ldMB, rec:%ldMB, skip times\n", 
                cur_pid, cur_comm, 
                MB(memnotify_get_free()),
    	        MB(memnotify_get_reclaimable())
    	        );
    	    
			return -1;
		}//if

#if(1)
        //check for amount of already set TIF_MEMDIE tasks + free
		if ( force_kill && !check_threshold(memnotify_leave_threshold_mb) ) { //total_selected_tasksize_mb
            task_unlock(p);
			rcu_read_unlock();

            LOG_PRINT(LOG_LV_VERBOSE, "[Lv3][LEAVE3] cur_pid=%d(%s), free:%ldMB, rec:%ldMB, MEMDIEs:%dMB\n", 
                cur_pid, cur_comm, 
                MB(memnotify_get_free()),
    	        MB(memnotify_get_reclaimable()),
    	        total_selected_tasksize_mb
    	        );
    	      
			return -1;
		}//if
		
        //already set TIF_MEMDIE tasks	
		if (test_tsk_thread_flag(p, TIF_MEMDIE)) {
			total_selected_tasksize_mb += MB(tasksize);
			//pr_crit("task_size=%dMB, pid=%d(%s), total=%d", MB(tasksize), p->pid, p->comm, total_selected_tasksize_mb);
			goto next_task;
		}//if
#endif
		
		if (tasksize <= 0) {
			goto next_task;
		}//if


#ifdef CONFIG_SLP_LOWMEM_NOTIFY_PRELOAD
		/* A task with highest preload priority is selected first */
		preload_prio = p->signal->preload_prio;
		if (preload_prio < selected_preload_prio)
			goto next_task;
		else if (preload_prio > selected_preload_prio)
			goto select_task;
#endif
		/*
		 * A task with highest oom_adj is selected among the tasks with
		 * the same preload priority.
		 */
		if (oom_adj < selected_oom_adj)
			goto next_task;
		else if (oom_adj > selected_oom_adj)
			goto select_task;

		/*
		 * A task with highest RSS is selected among the tasks with
		 * with the same oom_adj.
		 */
		if (tasksize <= selected_tasksize)
			goto next_task;
			
select_task:
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_adj = oom_adj;
#ifdef CONFIG_SLP_LOWMEM_NOTIFY_PRELOAD
		selected_preload_prio = preload_prio;
#endif
next_task:
        sum_of_rss_mb+= MB(tasksize);
		task_unlock(p);
	}//for_each_process
	sum_of_rss_mb-= total_selected_tasksize_mb;
	
	if (selected) {
        get_task_struct(selected);
        selected_pid= task_pid_nr(selected);
        strncpy(selected_comm, selected->comm, sizeof(selected_comm)-1);        
    }//if

    rcu_read_unlock();



	if (selected != NULL && force_kill) {
        //last checking for whether victim will be killed or not.
        if( atomic_read(&memnotify_last_threshold)!=THRESHOLD_DEADLY)  {
            LOG_PRINT(LOG_LV_VERBOSE, "[Lv3][LEAVE4] cur_pid=%d(%s), cur_threshold is not deadly. free:%ldMB, rec:%ldMB\n", 
                cur_pid, cur_comm, 
                MB(memnotify_get_free()),
    	        MB(memnotify_get_reclaimable())
    	        );

            ret_pid= cur_pid;
            goto out;
        }//if
        else if( !check_threshold(memnotify_leave_threshold_mb) ) { //total_selected_tasksize_mb
            LOG_PRINT(LOG_LV_VERBOSE, "[Lv3][Lv3][LEAVE5] cur_pid=%d(%s), free:%ldMB, rec:%ldMB, MEMDIEs:%dMB\n", 
                cur_pid, cur_comm, 
                MB(memnotify_get_free()),
    	        MB(memnotify_get_reclaimable()),
    	        total_selected_tasksize_mb
    	        );
    	        
            ret_pid= total_selected_tasksize_mb!=0 ? -1 : cur_pid;
            goto out;
        }//if
        else {
    		set_tsk_thread_flag(selected, TIF_MEMDIE);
    		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, selected, true);
   	    
    		LOG_PRINT(LOG_LV_INFO, "[Lv3][Lv3][SELECTED] cur_pid=%d(%s) sent sigkill to pid=%d(%s, adj=%d)\n", 
    		    cur_pid, cur_comm, selected_pid, selected_comm, selected_oom_adj);
    		    
    	    print_stat(oom_type_stat, LOG_LV_INFO);    		    
    
            lowmem_deathpending_timeout = jiffies + 3*HZ; //skip 3sec
            
            //we make chance to reclaim victim.
            ret_pid= -1;
            goto out;
        }//else
    }//if
	else if( selected!=NULL && !force_kill ) {  //( selected!=NULL && !force_kill )
		LOG_PRINT(LOG_LV_FORCE, "[Lv2] Victim task is pid=%d(%s), adj=%d, size=%dKB\n",
			selected_pid, selected_comm,
			selected_oom_adj, K(selected_tasksize)
		);
        
        print_stat(oom_type_stat, LOG_LV_FORCE);
        
		ret_pid = selected_pid;
        
		/* dump current memory state of all system tasks, only once after reboot,
		 * when [LMN][Lv2] occurs. */
		if (fault_task_only_once == 0) {
            #ifdef CONFIG_KPI_SYSTEM_SUPPORT
			    set_kpi_fault(0, 0, "LMN2", "LMN2");
            #endif
            
            #ifdef CONFIG_SAVELOGD			
                save_write2(NULL, "Z r 0 LMN2\0", 10, NULL);
            #endif
            
			fault_task_only_once++;
		}//if
		
		goto out;
	}//else if
    else {  // (selected==NULL)
		//ERR_PRINT("No Victim !!!\n");   //TODO
		if( force_kill ) {
		    //This is versy serious case, oom_adj=-17 process has leak and their were full in mem.
		    //there are remained free memory, but can not leave from deadly case of LMN.
		    //so, LMN will be called continuously, all of normal processes will be dead by LMN.
            //reboot or changing thersholds dynamically are good choice
            //or disabling LMN, or kill current process(oom_adj=-17) are also good.
            #if(1)  //TODO b_off_when_lowmem
                static unsigned long __dump_log_timeout= 0;
                        
                if( time_after_eq(jiffies, __dump_log_timeout) ) {
    		        dump_tasks(NULL, NULL, NULL);
                    //b_enable= 0;    //disable LMN, TODO
                    ERR_PRINT("No Victim. LMN was disabled.\n\n");
                    __dump_log_timeout = jiffies + 3*HZ; //consider of kernel reclaim time and app launching time, 3sec
                }//if
            #endif
            
            //TODO  in no victim case, all memory allocation will be sleep with ret_pid=-1
            //when re-run it(oom_adj!=-17) will kill itself. so at this point we return fast without sleep.
            ret_pid= 0;    
		}//if
		else {
		    ret_pid= 0;
		}//else
		
        goto out;
	}//else

	
out:	
	if( selected ) {
        put_task_struct(selected);
    }//if
	
	return ret_pid;
}



//TODO
void memnotify_threshold(gfp_t gfp_mask, int nr_pages)
{
	long reclaimable, real_free;
	int threshold;
	int last_threshold;
	int i;
	int ret= 0;
	char cur_comm[TASK_COMM_LEN];
	int cur_pid= -1;

    	
    if( !b_drv_init ) return;

    if( !b_enable ) return;

#if(1)  //TODO, 0: all of process participate in selecting victim. (in deadly case, all of allcation will be slow)
    if( !(gfp_mask & __GFP_WAIT) ) return;
#endif


	real_free = MB(memnotify_get_free());
	reclaimable = MB(memnotify_get_reclaimable());
	threshold = THRESHOLD_NORMAL;
	last_threshold = atomic_read(&memnotify_last_threshold);

	if (last_threshold >= NR_MEMNOTIFY_LEVEL)
		last_threshold = NR_MEMNOTIFY_LEVEL - 1;

	/* we save the tasks and rss info when free memory size is minimum,
	 * which means total used memory is highest at that moment. */
	if (check_peak && real_free<min_real_free ) {
	        min_real_free = real_free;
	        save_task_rss();
    }//if
        

	/* Obtain enter threshold level */
	for (i = (NR_MEMNOTIFY_LEVEL - 1); i >= 0; i--) {
		if ( check_threshold(memnotify_enter_thresholds_mb[i]) ) {
			threshold = i;
			break;
		}
	}
	/* return quickly when normal case */
	if (likely(threshold == THRESHOLD_NORMAL &&
		   last_threshold == THRESHOLD_NORMAL)) {
		goto out;
	}//if

	/* Need to leave a threshold by a certain margin. */
	if (threshold < last_threshold) {
		if ( check_threshold(memnotify_leave_threshold_mb) ) {
			threshold = last_threshold;
		}//if
	}//if

    atomic_set(&memnotify_last_threshold, threshold);

	if ( (real_free+reclaimable)!=0 &&  unlikely(threshold == THRESHOLD_DEADLY)) {
        //so many process to want to get free memory will race without below mutex.
        //because before a victim is not set to TIF_MEMDIE at end of this code by a process requiring memory, 
        //another victim is selected by another process requiring memory.
        //Without this mutex, so many stacked victims will be selected.
        //Also Without this mutex, different processes can select same victim. if it is in low memory situation
        //after return this func, oom killer can be called.
       
        int preemtable= preemptible();
        if( preemtable && (gfp_mask & __GFP_WAIT) ) mutex_lock(&victim_lock);
        else preempt_disable();
        if( (gfp_mask & __GFP_WAIT) && !preemtable ) {
            WARN(true, "memnotify_threshold: __GFP_WAIT and not preemtable \n");
        }//if
    

        //cause printk, we need to copy cur_comm
	    do {
    	    cur_pid= task_pid_nr(current);
    	    strncpy(cur_comm, current->comm, sizeof(cur_comm)-1);
    	} while(0);
        
        //TODO
		LOG_PRINT(LOG_LV_TRACE, "++ System memory is VERY LOW - pid:%d(%s), free:%ldMB, reclaim:%ldMB\n", 
		    cur_pid, cur_comm, real_free, reclaimable);

    	/*
    	 * If current has a pending SIGKILL or is exiting, then automatically
    	 * select it.  The goal is to allow it to allocate so that it may
    	 * quickly exit and free its memory.
    	 */
    	if (fatal_signal_pending(current) || current->flags & PF_EXITING) {
    		set_thread_flag(TIF_MEMDIE);
    		LOG_PRINT(LOG_LV_VERBOSE, "[Lv3][LEAVE0] cur_pid=%d(%s) kills itself.\n", cur_pid, cur_comm);
    		ret= cur_pid;
        }//if
        else {
            if( !check_threshold(memnotify_leave_threshold_mb) ) {
                LOG_PRINT(LOG_LV_VERBOSE, "[Lv3][LEAVE1] cur_pid=%d(%s), free:%ldMB, reclaim:%ldMB, already reclaimed.\n", 
                    cur_pid, cur_comm, 
                    MB(memnotify_get_free()),
        	        MB(memnotify_get_reclaimable())
        	        );
                ret= cur_pid;
            }//if
            else {
                ret= memnotify_victim_task(cur_pid, cur_comm, true, gfp_mask);
            }//else
    	}//else
		
        //TODO
		LOG_PRINT(LOG_LV_TRACE, "-- System memory is VERY LOW - pid:%d(%s), free:%ldMB, reclaim:%ldMB (ret=%d)\n\n", 
		    cur_pid, cur_comm, MB(memnotify_get_free()), MB(memnotify_get_reclaimable()), ret);

        if( preemtable && (gfp_mask & __GFP_WAIT) ) mutex_unlock(&victim_lock);
        else preempt_enable();
            
		goto out;
	}//if


out:
    //prohibit sleep & scheduled func. we don't know ISR? preemtion?
    if( ret<0 ) {
        //after return this func, current process try to allocate phsycial memory, 
        //if there is no free memory, oom_killer will be called.
        //if it will be able to reclaim, current process will try to re-allocate again after reclaimming
        victim_scheduled(gfp_mask);
    }//if    


    //after return this func, current process try to allocate phsycial memory, 
    //if there is no free memory, oom_killer will be called.
    //if it will be able to reclaim, current process will try to re-allocate again after reclaimming.

	// Rate limited notification of threshold changes.
	if( last_threshold != threshold) {
	    memnotify_wakeup(threshold);
	}//if
}
EXPORT_SYMBOL(memnotify_threshold);

static int lowmemnotify_open(struct inode *inode, struct file *file)
{
	struct memnotify_file_info *info;
	int err = 0;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}

	info->file = file;
	info->nr_events = 0;
	file->private_data = info;
	atomic_inc(&nr_watcher_task);
 out:
	return err;
}

static int lowmemnotify_release(struct inode *inode, struct file *file)
{
	struct memnotify_file_info *info = file->private_data;

	kfree(info);
	atomic_dec(&nr_watcher_task);
	return 0;
}

static unsigned int lowmemnotify_poll(struct file *file, poll_table * wait)
{
	unsigned int retval = 0;
	struct memnotify_file_info *info = file->private_data;
	int threshold;

	poll_wait(file, &memnotify_wait, wait);

	threshold = atomic_read(&memnotify_last_threshold);

	if (info->last_threshold != threshold) {
		info->last_threshold = threshold;
		retval = POLLIN;
		info->nr_events++;

		pr_info("%s (%d%%, Used %ldMB, Free %ldMB, %s)\n",
			__func__,
			(int)(MB(memnotify_get_used()) * 100
			      / MB(memnotify_get_total())),
			MB(memnotify_get_used()),
			MB(memnotify_get_free()),
			threshold_string(threshold));

	} else if (info->nr_events > 0)
		retval = POLLIN;

	return retval;
}

static ssize_t lowmemnotify_read(struct file *file,
				 char __user *buf, size_t count, loff_t *ppos)
{
	int threshold;
	unsigned long data;
	ssize_t ret = 0;
	struct memnotify_file_info *info = file->private_data;

	if (count < sizeof(unsigned long))
		return -EINVAL;

	threshold = atomic_read(&memnotify_last_threshold);
	data = memnotify_messages[threshold];

	ret = put_user(data, (unsigned long __user *)buf);
	if (0 == ret) {
		ret = sizeof(unsigned long);
		info->nr_events = 0;
	}
	return ret;
}

static ssize_t meminfo_show(struct class *class, struct class_attribute *attr,
			    char *buf)
{
	unsigned long used;
	unsigned long total_mem;
	int used_ratio;

	int last_threshold;

	int len = 0;
	int i;

	used = memnotify_get_used();
	total_mem = memnotify_get_total();

	last_threshold = atomic_read(&memnotify_last_threshold);

	used_ratio = used * 100 / total_mem;

	len += snprintf(buf + len, PAGE_SIZE,
			"Total RAM size: \t%15ld MB( %6ld kB)\n",
			MB(totalram_pages), K(totalram_pages));

	len += snprintf(buf + len, PAGE_SIZE,
			"Used (Mem+Swap): \t%15ld MB( %6ld kB)\n", MB(used),
			K(used));

	len +=
	    snprintf(buf + len, PAGE_SIZE,
		     "Used (Mem):  \t\t%15ld MB( %6ld kB)\n", MB(used),
		     K(used));

	len +=
	    snprintf(buf + len, PAGE_SIZE,
		     "Used (Swap): \t\t%15ld MB( %6ld kB)\n",
		     MB(total_swap_pages - get_nr_swap_pages()),
		     K(total_swap_pages - get_nr_swap_pages()));

	len += snprintf(buf + len, PAGE_SIZE,
			"Used Ratio: \t\t%15d  %%\n", used_ratio);

	len +=
	    snprintf(buf + len, PAGE_SIZE, "Mem Free:\t\t%15ld MB( %6ld kB)\n",
		     MB((long)(global_page_state(NR_FREE_PAGES))),
		     K((long)(global_page_state(NR_FREE_PAGES))));

	len += snprintf(buf + len, PAGE_SIZE,
			"Available (Free+Reclaimable):%10ld MB( %6ld kB)\n",
			MB((long)(total_mem - used)),
			K((long)(total_mem - used)));

	len += snprintf(buf + len, PAGE_SIZE,
			"Reserve Page:\t\t%15ld MB( %6ld kB)\n",
			MB((long)(totalreserve_pages)),
			K((long)(totalreserve_pages)));

	len += snprintf(buf + len, PAGE_SIZE,
			"Last Threshold: [%s]\n",
			threshold_string(last_threshold));

	len += snprintf(buf + len, PAGE_SIZE,
			"Enter Thresholds \n");
	for (i = 0; i < NR_MEMNOTIFY_LEVEL; i++) {
		unsigned long limit = MB(total_mem) -
		    memnotify_enter_thresholds_mb[i];
		long left = limit - MB(used);
		len += snprintf(buf + len, PAGE_SIZE,
				"[%s] threshold:%5dMB, total:%5ldMB, used:%5ldMB, free:%5ldMB, remained_to_th:%5ldMB\n",
				threshold_string(i),
				memnotify_enter_thresholds_mb[i], MB(total_mem), MB(used), MB(total_mem)-MB(used), left);

	}
	len += snprintf(buf + len, PAGE_SIZE,
			"Leave Thresholds \n");
	for (i = 0; i < NR_MEMNOTIFY_LEVEL; i++) {
		unsigned long limit = MB(total_mem) -
		    memnotify_leave_threshold_mb;
		long left = limit - MB(used);
		len += snprintf(buf + len, PAGE_SIZE,
				"[%s] threshold:%5dMB, total:%5ldMB, used:%5ldMB, free:%5ldMB, remained_to_th:%5ldMB\n",
				threshold_string(i),
				memnotify_leave_threshold_mb, MB(total_mem), MB(used), MB(total_mem)-MB(used),left);
	}

	len += snprintf(buf + len, PAGE_SIZE,
			"threshold_margin_mb=%uMB\n", threshold_margin_mb);

	return len;
}


static void set_thresholds(long deadly_enter_mb, long margin_mb)
{
    
    mutex_lock(&victim_lock);
    
    if( deadly_enter_mb>=0 ) {
        memnotify_enter_thresholds_mb[THRESHOLD_DEADLY]= deadly_enter_mb;
    }//if
    
    if( margin_mb>=0 ) {
        threshold_margin_mb= margin_mb;
    }//if
	
	memnotify_enter_thresholds_mb[THRESHOLD_CRITICAL] = memnotify_enter_thresholds_mb[THRESHOLD_DEADLY]+threshold_margin_mb;
	memnotify_enter_thresholds_mb[THRESHOLD_LOW] = memnotify_enter_thresholds_mb[THRESHOLD_CRITICAL]+threshold_margin_mb;
	memnotify_leave_threshold_mb= memnotify_enter_thresholds_mb[THRESHOLD_LOW]+threshold_margin_mb;
	
    mutex_unlock(&victim_lock);
	
    pr_crit("[LMN] threshold margin set : %d MB\n", threshold_margin_mb);
	pr_crit("[LMN] lv3 threshold set    : %d MB\n", memnotify_enter_thresholds_mb[THRESHOLD_DEADLY]);
	pr_crit("[LMN] lv2 threshold set    : %d MB\n", memnotify_enter_thresholds_mb[THRESHOLD_CRITICAL]);	
	pr_crit("[LMN] lv1 threshold set    : %d MB\n", memnotify_enter_thresholds_mb[THRESHOLD_LOW]);	
	pr_crit("[LMN] leave threshold set  : %d MB\n", memnotify_leave_threshold_mb);
	

}

static ssize_t th_margin_store(struct class *class, struct class_attribute *attr,
			   const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		threshold_margin_mb = val;
		set_thresholds(-1, val);		
	}
	return ret;
}

static ssize_t threshold_lv3_store(struct class *class, struct class_attribute *attr,
			    const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		
		set_thresholds(val, -1);
	}
	return ret;
}


static ssize_t victim_task_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	int len = 0;
	int pid;

	dump_tasks(NULL, NULL, NULL);
	pid = memnotify_victim_task(0, "victim_task_show", false, 0);
	
	len += snprintf(buf + len, PAGE_SIZE, "%d", pid);

	return len;
}

static ssize_t peak_rss_show(struct class *class,
			     struct class_attribute *attr, char *buf)
{
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE, "  PID\tRSS(KB)\t NAME\n");
	len += snprintf(buf + len, PAGE_SIZE,
			"=================================\n");
	for (i = 0; i < nr_tasks; i++) {
		len += snprintf(buf + len, PAGE_SIZE, "%5d\t%6ld\t %s\n",
				task_rss[i].pid, K(task_rss[i].rss),
				task_rss[i].comm);
	}//for


	return (ssize_t) len;
}


static ssize_t peak_rss_store(struct class *class, struct class_attribute *attr,
			      const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		if (val) {
			check_peak = 1;
			save_task_rss();
		} else {
			check_peak = 0;
		}
	}
	return ret;
}


static ssize_t enable_show(struct class *class,
			     struct class_attribute *attr, char *buf)
{
	int len = 0;


	len += snprintf(buf + len, PAGE_SIZE, "%d\n", b_enable);

	return (ssize_t) len;
}

static ssize_t enable_store(struct class *class, struct class_attribute *attr,
			      const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		if (val) {
			b_enable= 1;
		} else {
			b_enable= 0;
		}
	}
	return ret;
}


static ssize_t oom_store(struct class *class, struct class_attribute *attr,
			      const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
		if (val) {
            out_of_memory(node_zonelist(first_online_node, GFP_KERNEL), GFP_KERNEL, 0, NULL, true);
		} 
	}
	return ret;
}

static ssize_t loglevel_show(struct class *class,
			     struct class_attribute *attr, char *buf)
{
	int len = 0;

    pr_crit("LOG_LV_OFF:%d\n", LOG_LV_OFF);
    pr_crit("LOG_LV_INFO:%d\n", LOG_LV_INFO);
    pr_crit("LOG_LV_VERBOSE:%d\n", LOG_LV_VERBOSE);
    pr_crit("LOG_LV_TRACE:%d\n", LOG_LV_TRACE);
    pr_crit("current s_log_level=%d\n", s_log_level);

	len += snprintf(buf + len, PAGE_SIZE, "%d\n", s_log_level);

	return (ssize_t) len;
}


static ssize_t loglevel_store(struct class *class, struct class_attribute *attr,
			      const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	long val;
	ret = kstrtol(buf, 10, &val);
	if (ret == 0) {
		ret = count;
        s_log_level= val;
        pr_crit("current s_log_level=%d\n", s_log_level);
	}
	else {
	    pr_crit("error. current s_log_level=%d\n", s_log_level);
	}
	return ret;
}

static CLASS_ATTR(meminfo, S_IRUGO, meminfo_show, NULL);
static CLASS_ATTR(threshold_lv3, S_IWUSR, NULL, threshold_lv3_store);
static CLASS_ATTR(th_margin, S_IWUSR, NULL, th_margin_store);
static CLASS_ATTR(victim_task, S_IRUGO | S_IWUSR, victim_task_show, NULL);
static CLASS_ATTR(check_peak_rss, S_IRUGO | S_IWUSR, peak_rss_show, peak_rss_store);
static CLASS_ATTR(enable, S_IRUGO | S_IWUSR, enable_show, enable_store);
static CLASS_ATTR(oom, S_IWUSR, NULL, oom_store);
static CLASS_ATTR(loglevel, S_IRUGO | S_IWUSR, loglevel_show, loglevel_store);


static const struct file_operations memnotify_fops = {
	.open = lowmemnotify_open,
	.release = lowmemnotify_release,
	.read = lowmemnotify_read,
	.poll = lowmemnotify_poll,
};

static struct device *memnotify_device;
static struct class *memnotify_class;
static int memnotify_major = -1;

static int __init lowmemnotify_init(void)
{
	int err;

	pr_info("[LMN] Low Memory Notify loaded\n");
	
	mutex_init(&victim_lock);

	memnotify_enter_thresholds_mb[0] = MB(totalram_pages);

	memnotify_major = register_chrdev(0, MEMNOTIFY_DEVICE, &memnotify_fops);
	if (memnotify_major < 0) {
		pr_err("Unable to get major number for memnotify dev\n");
		err = -EBUSY;
		goto error_create_chr_dev;
	}

	memnotify_class = class_create(THIS_MODULE, MEMNOTIFY_DEVICE);
	if (IS_ERR(memnotify_class)) {
		err = PTR_ERR(memnotify_class);
		goto error_class_create;
	}

	memnotify_device =
	    device_create(memnotify_class, NULL, MKDEV(memnotify_major, 0),
			  NULL, MEMNOTIFY_DEVICE);

	if (IS_ERR(memnotify_device)) {
		err = PTR_ERR(memnotify_device);
		goto error_create_class_dev;
	}

	err = class_create_file(memnotify_class, &class_attr_meminfo);
	if (err) {
		pr_err("%s: couldn't create meminfo.\n", __func__);
		goto error_create_meminfo_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_threshold_lv3);
	if (err) {
		pr_err("%s: couldn't create threshold level 3.\n", __func__);
		goto error_create_threshold_lv3_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_th_margin);
	if (err) {
		pr_err("%s: couldn't create threshold margin.\n", __func__);
		goto error_create_threshold_margin_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_victim_task);
	if (err) {
		pr_err("%s: couldn't create victim sysfs file.\n", __func__);
		goto error_create_victim_task_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_check_peak_rss);
	if (err) {
		pr_err("%s: couldn't create peak rss sysfs file.\n", __func__);
		goto error_create_peak_rss_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_enable);
	if (err) {
		pr_err("%s: couldn't create enable sysfs file.\n", __func__);
		goto error_create_enable_class_file;
	}

	err = class_create_file(memnotify_class, &class_attr_oom);
	if (err) {
		pr_err("%s: couldn't create oom sysfs file.\n", __func__);
		goto error_create_oom_class_file;
	}
	
	err = class_create_file(memnotify_class, &class_attr_loglevel);
	if (err) {
		pr_err("%s: couldn't create loglevel sysfs file.\n", __func__);
		goto error_create_loglevel_class_file;
	}	

	// set initial free memory with total memory size 
	min_real_free= MB(memnotify_get_total());

    b_drv_init= 1;

	return 0;
error_create_loglevel_class_file:
    class_remove_file(memnotify_class, &class_attr_oom);
error_create_oom_class_file:
    class_remove_file(memnotify_class, &class_attr_enable);
 error_create_enable_class_file:
	class_remove_file(memnotify_class, &class_attr_check_peak_rss);
 error_create_peak_rss_class_file:
	class_remove_file(memnotify_class, &class_attr_victim_task);
 error_create_victim_task_class_file:
	class_remove_file(memnotify_class, &class_attr_th_margin);
 error_create_threshold_margin_class_file:
	class_remove_file(memnotify_class, &class_attr_threshold_lv3);
 error_create_threshold_lv3_class_file:
	class_remove_file(memnotify_class, &class_attr_meminfo);
 error_create_meminfo_class_file:
	device_del(memnotify_device);
 error_create_class_dev:
	class_destroy(memnotify_class);
 error_class_create:
	unregister_chrdev(memnotify_major, MEMNOTIFY_DEVICE);
 error_create_chr_dev:
	return err;
}

static void __exit lowmemnotify_exit(void)
{
    
    b_drv_init= 0;
    
	if (memnotify_device)
		device_del(memnotify_device);
	if (memnotify_class)
		class_destroy(memnotify_class);
	if (memnotify_major >= 0)
		unregister_chrdev(memnotify_major, MEMNOTIFY_DEVICE);
		
    mutex_destroy(&victim_lock);
}

module_init(lowmemnotify_init);
module_exit(lowmemnotify_exit);

MODULE_LICENSE("GPL");
