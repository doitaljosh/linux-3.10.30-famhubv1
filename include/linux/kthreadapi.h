/*
 *
 * Filename: include/linux/kthreadapi.h
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * Kernel threads management generic wrappers head file
 */
#ifndef _KTHREAD_FNAPI_H_
#define _KTHREAD_FNAPI_H_

#define KTHREAD_INUSE
/*
 * Define EXTERN macro exactly once only in a c source file before including the header file pr_time.h.
 * In rest of the c source files, there is no need to define EXTERN
 */
#ifndef EXTERN
#define EXTERN extern
#endif



struct k_info {
int wait_condition_flag;
struct task_struct *tsk;
wait_queue_head_t waitQ;
int (* threadfn)(void *);
char * kname;
void *data;
};

EXTERN void create_run_kthread(struct k_info *);
EXTERN void stop_kthread(struct k_info *);
EXTERN int kthreadfn(void *pinfo);
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
EXTERN void user_port_reset_thread(void *pinfo);
#endif
#endif  // _KTHREAD_FNAPI_H_

