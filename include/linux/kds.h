#ifndef _KDS_H_
#define _KDS_H_

#include <linux/list.h>
#include <linux/workqueue.h>

#define KDS_WAIT_BLOCKING (ULONG_MAX)

/* what to do when waitall must wait for a synchronous locked resource: */
#define KDS_FLAG_LOCKED_FAIL    (0u <<  0) /* fail waitall */
#define KDS_FLAG_LOCKED_IGNORE  (1u <<  0) /* don't wait, but block other
that waits */
#define KDS_FLAG_LOCKED_WAIT    (2u <<  0) /* wait (normal */
#define KDS_FLAG_LOCKED_ACTION  (3u <<  0) /* mask to extract the action to
do on locked resources */

struct kds_resource_set;

typedef void (*kds_callback_fn) (void * callback_parameter, void *
callback_extra_parameter);

struct kds_callback
{
	kds_callback_fn  user_cb; /* real cb */
	int direct;               /* do direct or queued call? */
	struct workqueue_struct * wq;
};

struct kds_link
{
	struct kds_resource_set * parent;
	struct list_head          link;
	unsigned long             state;
};

struct kds_resource
{
	struct kds_link waiters;
	unsigned long lock_count;
};

struct kds_dma_buf_register {
	void (*kds_callback_term)(struct kds_callback *cb);
	int (*kds_resource_term)(struct kds_resource *res);
	struct kds_resource_set *(*kds_waitall)(int  number_resources,unsigned long  *exclusive_access_bitmap,struct kds_resource **resource_list,unsigned long jiffies_timeout);
	void (*kds_resource_set_release)(struct kds_resource_set **pprset);
	int (*kds_async_waitall)(struct kds_resource_set **pprset,unsigned long flags,struct kds_callback *cb,void  *callback_parameter,void   *callback_extra_parameter,int  number_resources,unsigned long  *exclusive_access_bitmap,struct kds_resource **resource_list);
	void (*kds_resource_init)(struct kds_resource *res);
	int (*kds_callback_init)(struct kds_callback *cb, int direct, kds_callback_fn user_cb);
};
/* callback API */

/* Initialize a callback object.
 *
 * Typically created per context or per hw resource.
 *
 * Callbacks can be performed directly if no nested locking can
 * happen in the client.
 *
 * Nested locking can occur when a lock is held during the
kds_async_waitall or
 * kds_resource_set_release call. If the callback needs to take the same
lock
 * nested locking will happen.
 *
 * If nested locking could happen non-direct callbacks can be requested.
 * Callbacks will then be called asynchronous to the triggering call.
 */
int kds_callback_init(struct kds_callback * cb, int direct, kds_callback_fn
user_cb);

/* Terminate the use of a callback object.
 *
 * If the callback object was set up as non-direct
 * any pending callbacks will be flushed first.
 * Note that to avoid a deadlock the lock callbacks needs
 * can't be held when a callback object is terminated.
 */
void kds_callback_term(struct kds_callback * cb);


/* resource object API */

/* initialize a resource handle for a shared resource */
void kds_resource_init(struct kds_resource * resource);

/*
 * Will assert if the resource is being used or waited on.
 * The caller should NOT try to terminate a resource that could still have
clients.
 * After the function returns the resource is no longer known by kds.
 */
int  kds_resource_term(struct kds_resource * resource);

/* Asynchronous wait for a set of resources.
 * Callback will be called when all resources are available.
 * If all the resources was available the callback will be called before
kds_async_waitall returns.
 * So one must not hold any locks the callback code-flow can take when
calling kds_async_waitall.
 * Caller considered to own/use the resources until \a kds_rset_release is
called.
 * flags is one or more of the KDS_FLAG_* set.
 * exclusive_access_bitmap is a bitmap where a high bit means exclusive
access while a low bit means shared access.
 * Use the Linux __set_bit API, where the index of the buffer to control is
used as the bit index.
 *
 * Standard Linux error return value.
 */
int kds_async_waitall(
               struct kds_resource_set ** pprset,
               unsigned long              flags,
               struct kds_callback *      cb,
               void *                     callback_parameter,
               void *                     callback_extra_parameter,
               int                        number_resources,
               unsigned long *            exclusive_access_bitmap,
               struct kds_resource **     resource_list);

/* Synchronous wait for a set of resources.
 * Function will return when one of these have happened:
 * - all resources have been obtained
 * - timeout lapsed while waiting
 * - a signal was received while waiting
 *
 * Caller considered to own/use the resources when the function returns.
 * Caller must release the resources using \a kds_rset_release.
 *
 * Calling this function while holding already locked resources or other
locking primitives is dangerous.
 * One must if this is needed decide on a lock order of the resources
and/or the other locking primitives
 * and always take the resources/locking primitives in the specific order.
 *
 * Use the ERR_PTR framework to decode the return value.
 * NULL = time out
 * If IS_ERR then PTR_ERR gives:
 *  ERESTARTSYS = signal received, retry call after signal
 *  all other values = internal error, lock failed
 * Other values  = successful wait, now the owner, must call
kds_resource_set_release
 */
struct kds_resource_set * kds_waitall(
               int                    number_resources,
               unsigned long *        exclusive_access_bitmap,
               struct kds_resource ** resource_list,
               unsigned long          jifies_timeout);

/* Release resources after use.
 * Caller must handle that other async callbacks will trigger,
 * so must avoid holding any locks a callback will take.
 *
 * The function takes a pointer to your poiner to handle a race
 * between a cancelation and a completion.
 *
 * If the caller can't guarantee that a race can't occur then
 * the passed in pointer must be the same in both call paths
 * to allow kds to manage the potential race.
 */
void kds_resource_set_release(struct kds_resource_set ** pprset);

/* set dynamic loading of kds functions to DMA_BUF on kds initialization. */
int __init kds_initialize_dma_buf(void);
#endif /* _KDS_H_ */

