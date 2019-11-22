/*
 * Filename: drivers/usb/core/kthreadapi.c
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * This file provides generic wrapper functions to create and stop kernel threads 
 * using wait queues.
 */ 

#include <linux/kthread.h>
#include <linux/kthreadapi.h>

#undef DEBUG

#ifdef DEBUG
        #define MSG(string, args...) \
                        printk(KERN_EMERG "%s:%d : " string, __FUNCTION__, __LINE__, ##args)
#else
        #define MSG(string, args...)
#endif

void create_run_kthread(struct k_info *);
void stop_kthread(struct k_info *);
int kthreadfn(void *pinfo);

void create_run_kthread(struct k_info *kinfo)
{
                init_waitqueue_head(&kinfo->waitQ);
		kinfo->wait_condition_flag = 0;
                printk(KERN_ALERT"%s kthread_run executing for thread name %s\n",__func__,kinfo->kname);
                kinfo->tsk = kthread_run(kinfo->threadfn, kinfo, kinfo->kname);
                if (IS_ERR(kinfo->tsk)) {
                        MSG("%s kthread_run fails:%s\n",__func__, kinfo->kname);
                }else
                        MSG("%s kthread_run success:%s\n",__func__, kinfo->kname);
}
void stop_kthread(struct k_info *kinfo)
{
        if(kinfo)
                if(kinfo->tsk)
		{
                        kthread_stop(kinfo->tsk);
			kinfo->tsk=NULL;
		}
}
