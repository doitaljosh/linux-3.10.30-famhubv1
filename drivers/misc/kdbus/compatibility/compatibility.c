/*
 * Samsung Electronics
 * TTV SBB project
 *
 * API missing or incompatible in kernel 3.8
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/export.h>

#include <linux/err.h>
#include <linux/string.h>
#include <linux/idr.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "compatibility.h"

/*
 * message.c patch
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
/*
 * COMPATIBILITY FUNCTION - removed from kernel 3.11
 *
 * FIXME: dirty and unsafe version of:
 *   http://git.kernel.org/cgit/linux/kernel/git/tj/cgroup.git/commit/?h=review-task_cgroup_path_from_hierarchy
 * remove it when the above is upstream.
 */
int task_cgroup_path_from_hierarchy(struct task_struct *task, int hierarchy_id,
            char *buf, size_t buflen)
{
  struct cg_cgroup_link {
    struct list_head cgrp_link_list;
    struct cgroup *cgrp;
    struct list_head cg_link_list;
    struct css_set *cg;
  };

  struct cgroupfs_root {
    struct super_block *sb;
    unsigned long subsys_mask;
    int hierarchy_id;
  };

  struct cg_cgroup_link *link;
  int ret = -ENOENT;

//  cgroup_lock();
  list_for_each_entry(link, &current->cgroups->cg_links, cg_link_list) {
    struct cgroup *cg = link->cgrp;
    struct cgroupfs_root *root = (struct cgroupfs_root *)cg->root;

    if (root->hierarchy_id != hierarchy_id)
      continue;

    ret = cgroup_path(cg, buf, (int)buflen);
    break;
  }
//  cgroup_unlock();

  return ret;
}
 
#endif

/* 
 *
 * device.h patch
 *
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
static void system_root_device_release(struct device *dev)
{
	kfree(dev);
}

static int subsys_register(struct bus_type *subsys,
			   const struct attribute_group **groups,
			   struct kobject *parent_of_root)
{
	struct device *dev;
	int err;

	err = bus_register(subsys);
	if (err < 0)
		return err;

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto err_dev;
	}

	err = dev_set_name(dev, "%s", subsys->name);
	if (err < 0)
		goto err_name;

	dev->kobj.parent = parent_of_root;
	dev->groups = groups;
	dev->release = system_root_device_release;

	err = device_register(dev);
	if (err < 0)
		goto err_dev_reg;

	subsys->dev_root = dev;
	return 0;

err_dev_reg:
	put_device(dev);
	dev = NULL;
err_name:
	kfree(dev);
err_dev:
	bus_unregister(subsys);
	return err;
}

int subsys_virtual_register(struct bus_type *subsys,
			    const struct attribute_group **groups)
{
	struct kobject *virtual_dir;
        struct kobject *(*get_virtual_device_parent_ptr)(struct device *dev) = NULL;

	get_virtual_device_parent_ptr = get_virtual_device_parent();
 
         virtual_dir = (*get_virtual_device_parent_ptr)(NULL);
         if (!virtual_dir)
                 return -ENOMEM;
 
         return subsys_register(subsys, groups, virtual_dir);
}
#endif

/* 
 *
 * IDR patch
 *
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
//#define MAX_IDR_SHIFT		(sizeof(int) * 8 - 1)
#define MAX_IDR_BIT		(1U << MAX_IDR_SHIFT)

/* Leave the possibility of an incomplete final layer */
#define MAX_IDR_LEVEL ((MAX_IDR_SHIFT + IDR_BITS - 1) / IDR_BITS)

/* Number of id_layer structs to leave in free list */
#define MAX_IDR_FREE (MAX_IDR_LEVEL * 2)

static DEFINE_PER_CPU(struct idr_layer *, idr_preload_head);
static DEFINE_PER_CPU(int, idr_preload_cnt);

/* the maximum ID which can be allocated given idr->layers */
static int idr_max(int layers)
{
	int bits = min_t(int, layers * IDR_BITS, MAX_IDR_SHIFT);

	return (1 << bits) - 1;
}

static struct idr_layer *get_from_free_list(struct idr *idp)
{
	struct idr_layer *p;
	unsigned long flags;

	spin_lock_irqsave(&idp->lock, flags);
	if ((p = idp->id_free)) {
		idp->id_free = p->ary[0];
		idp->id_free_cnt--;
		p->ary[0] = NULL;
	}
	spin_unlock_irqrestore(&idp->lock, flags);
	return(p);
}

static void __move_to_free_list(struct idr *idp, struct idr_layer *p)
{
	p->ary[0] = idp->id_free;
	idp->id_free = p;
	idp->id_free_cnt++;
}

/**
 * idr_layer_alloc - allocate a new idr_layer
 * @gfp_mask: allocation mask
 * @layer_idr: optional idr to allocate from
 *
 * If @layer_idr is %NULL, directly allocate one using @gfp_mask or fetch
 * one from the per-cpu preload buffer.  If @layer_idr is not %NULL, fetch
 * an idr_layer from @idr->id_free.
 *
 * @layer_idr is to maintain backward compatibility with the old alloc
 * interface - idr_pre_get() and idr_get_new*() - and will be removed
 * together with per-pool preload buffer.
 */
static struct idr_layer *idr_layer_alloc(gfp_t gfp_mask, struct idr *layer_idr)
{
	struct idr_layer *new;

	/* this is the old path, bypass to get_from_free_list() */
	if (layer_idr)
		return get_from_free_list(layer_idr);

	/*
	 * Try to allocate directly from kmem_cache.  We want to try this
	 * before preload buffer; otherwise, non-preloading idr_alloc()
	 * users will end up taking advantage of preloading ones.  As the
	 * following is allowed to fail for preloaded cases, suppress
	 * warning this time.
	 */
	new = kmem_cache_zalloc(idr_get_layer_cache(), gfp_mask | __GFP_NOWARN);
	if (new)
		return new;

	/*
	 * Try to fetch one from the per-cpu preload buffer if in process
	 * context.  See idr_preload() for details.
	 */
	if (!in_interrupt()) {
		preempt_disable();
		new = __this_cpu_read(idr_preload_head);
		if (new) {
			__this_cpu_write(idr_preload_head, new->ary[0]);
			__this_cpu_dec(idr_preload_cnt);
			new->ary[0] = NULL;
		}
		preempt_enable();
		if (new)
			return new;
	}

	/*
	 * Both failed.  Try kmem_cache again w/o adding __GFP_NOWARN so
	 * that memory allocation failure warning is printed as intended.
	 */
	return kmem_cache_zalloc(idr_get_layer_cache(), gfp_mask);
}

static int sub_alloc(struct idr *idp, int *starting_id, struct idr_layer **pa,
		     gfp_t gfp_mask, struct idr *layer_idr)
{
	int n, m, sh;
	struct idr_layer *p, *new;
	int l, id, oid;
	unsigned long bm;

	id = *starting_id;
 restart:
	p = idp->top;
	l = idp->layers;
	pa[l--] = NULL;
	while (1) {
		/*
		 * We run around this while until we reach the leaf node...
		 */
		n = (id >> (IDR_BITS*l)) & IDR_MASK;
		bm = ~p->bitmap;
		m = find_next_bit(&bm, IDR_SIZE, n);
		if (m == IDR_SIZE) {
			/* no space available go back to previous layer. */
			l++;
			oid = id;
			id = (id | ((1 << (IDR_BITS * l)) - 1)) + 1;

			/* if already at the top layer, we need to grow */
			if (id >= 1 << (idp->layers * IDR_BITS)) {
				*starting_id = id;
				return -EAGAIN;
			}
			p = pa[l];
			BUG_ON(!p);

			/* If we need to go up one layer, continue the
			 * loop; otherwise, restart from the top.
			 */
			sh = IDR_BITS * (l + 1);
			if (oid >> sh == id >> sh)
				continue;
			else
				goto restart;
		}
		if (m != n) {
			sh = IDR_BITS*l;
			id = ((id >> sh) ^ n ^ m) << sh;
		}
		if ((id >= MAX_IDR_BIT) || (id < 0))
			return -ENOSPC;
		if (l == 0)
			break;
		/*
		 * Create the layer below if it is missing.
		 */
		if (!p->ary[m]) {
			new = idr_layer_alloc(gfp_mask, layer_idr);
			if (!new)
				return -ENOMEM;
			new->layer = l-1;
			//new->prefix = id & idr_layer_prefix_mask(new->layer);
			rcu_assign_pointer(p->ary[m], new);
			p->count++;
		}
		pa[l--] = p;
		p = p->ary[m];
	}

	pa[l] = p;
	return id;
}

static int idr_get_empty_slot(struct idr *idp, int starting_id,
			      struct idr_layer **pa, gfp_t gfp_mask,
			      struct idr *layer_idr)
{
	struct idr_layer *p, *new;
	int layers, v, id;
	unsigned long flags;

	id = starting_id;
build_up:
	p = idp->top;
	layers = idp->layers;
	if (unlikely(!p)) {
		if (!(p = idr_layer_alloc(gfp_mask, layer_idr)))
			return -ENOMEM;
		p->layer = 0;
		layers = 1;
	}
	/*
	 * Add a new layer to the top of the tree if the requested
	 * id is larger than the currently allocated space.
	 */
	while (id > idr_max(layers)) {
		layers++;
		if (!p->count) {
			/* special case: if the tree is currently empty,
			 * then we grow the tree by moving the top node
			 * upwards.
			 */
			p->layer++;
			//WARN_ON_ONCE(p->prefix);
			continue;
		}
		if (!(new = idr_layer_alloc(gfp_mask, layer_idr))) {
			/*
			 * The allocation failed.  If we built part of
			 * the structure tear it down.
			 */
			spin_lock_irqsave(&idp->lock, flags);
			for (new = p; p && p != idp->top; new = p) {
				p = p->ary[0];
				new->ary[0] = NULL;
				new->bitmap = new->count = 0;
				__move_to_free_list(idp, new);
			}
			spin_unlock_irqrestore(&idp->lock, flags);
			return -ENOMEM;
		}
		new->ary[0] = p;
		new->count = 1;
		new->layer = layers-1;
		//new->prefix = id & idr_layer_prefix_mask(new->layer);
		if (bitmap_full(&p->bitmap, IDR_SIZE))
			__set_bit(0, &new->bitmap);
		p = new;
	}
	rcu_assign_pointer(idp->top, p);
	idp->layers = layers;
	v = sub_alloc(idp, &id, pa, gfp_mask, layer_idr);
	if (v == -EAGAIN)
		goto build_up;
	return(v);
}

static void idr_mark_full(struct idr_layer **pa, int id)
{
	struct idr_layer *p = pa[0];
	int l = 0;

	__set_bit(id & IDR_MASK, &p->bitmap);
	/*
	 * If this layer is full mark the bit in the layer above to
	 * show that this part of the radix tree is full.  This may
	 * complete the layer above and require walking up the radix
	 * tree.
	 */
	while (bitmap_full(&p->bitmap, IDR_SIZE)) {
		if (!(p = pa[++l]))
			break;
		id = id >> IDR_BITS;
		__set_bit((id & IDR_MASK), &p->bitmap);
	}
}

/*
 * @id and @pa are from a successful allocation from idr_get_empty_slot().
 * Install the user pointer @ptr and mark the slot full.
 */
static void idr_fill_slot(struct idr *idr, void *ptr, int id,
			  struct idr_layer **pa)
{
	/* update hint used for lookup, cleared from free_layer() */
	//rcu_assign_pointer(idr->hint, pa[0]);

	rcu_assign_pointer(pa[0]->ary[id & IDR_MASK], (struct idr_layer *)ptr);
	pa[0]->count++;
	idr_mark_full(pa, id);
}

/**
 * idr_alloc - allocate new idr entry
 * @idr: the (initialized) idr
 * @ptr: pointer to be associated with the new id
 * @start: the minimum id (inclusive)
 * @end: the maximum id (exclusive, <= 0 for max)
 * @gfp_mask: memory allocation flags
 *
 * Allocate an id in [start, end) and associate it with @ptr.  If no ID is
 * available in the specified range, returns -ENOSPC.  On memory allocation
 * failure, returns -ENOMEM.
 *
 * Note that @end is treated as max when <= 0.  This is to always allow
 * using @start + N as @end as long as N is inside integer range.
 *
 * The user is responsible for exclusively synchronizing all operations
 * which may modify @idr.  However, read-only accesses such as idr_find()
 * or iteration can be performed under RCU read lock provided the user
 * destroys @ptr in RCU-safe way after removal from idr.
 */
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp_mask)
{
	int max = end > 0 ? end - 1 : INT_MAX;	/* inclusive upper limit */
	struct idr_layer *pa[MAX_IDR_LEVEL + 1];
	int id;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	/* sanity checks */
	if (WARN_ON_ONCE(start < 0))
		return -EINVAL;
	if (unlikely(max < start))
		return -ENOSPC;

	/* allocate id */
	id = idr_get_empty_slot(idr, start, pa, gfp_mask, NULL);
	if (unlikely(id < 0))
		return id;
	if (unlikely(id > max))
		return -ENOSPC;

	idr_fill_slot(idr, ptr, id, pa);
	return id;
}
#endif

#if BITS_PER_LONG == 32
#ifndef div64_u64_rem
u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
    u32 high = (u32)(divisor >> 32);
    u64 quot;

    if (high == 0) {
        u32 rem32;
        quot = div_u64_rem(dividend, (u32)divisor, &rem32);
        *remainder = rem32;
    } else {
        int n = 1 + fls((int)high);
        quot = div_u64(dividend >> n, (u32)(divisor >> n));

        if (quot != 0)
            quot--;

        *remainder = dividend - quot * divisor;
        if (*remainder >= divisor) {
            quot++;
            *remainder -= divisor;
        }
    }

    return quot;
}
#endif
#endif
