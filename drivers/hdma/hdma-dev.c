/*
 * Hardware Dedicated Memory Allocator userspace driver
 * Copyright (c) 2011 by Samsung Electronics.
 * Written by Vasily Leonenko (v.leonenko@samsung.com)
 */

#define DEBUG
#define pr_fmt(fmt) "hdma: " fmt
#define HDMA_CHAR_MAJOR 182
#define HDMA_DEV_NAME "hdma"

#include <linux/errno.h>       /* Error numbers */
#include <linux/err.h>         /* IS_ERR_VALUE() */
#include <linux/fs.h>          /* struct file */
#include <linux/mm.h>          /* Memory stuff */
#include <linux/module.h>      /* Standard module stuff */
#include <linux/device.h>      /* struct device, dev_dbg() */
#include <linux/types.h>       /* Just to be safe ;) */
#include <linux/sched.h>

#include <linux/mmzone.h>
#include <linux/dma-contiguous.h>

#include <asm/setup.h>		/* meminfo */

#define IOCTL_HDMA_NOTIFY_ALL   _IOWR('p', 0, unsigned long)
#define IOCTL_HDMA_NOTIFY       _IOWR('p', 1, unsigned long)

#ifdef CONFIG_CMA_APP_ALLOC
# define IOCTL_HDMA_ALLOC_SET      _IOWR('p', 2, unsigned long)
# define IOCTL_HDMA_ALLOC_CLEAR    _IOWR('p', 3, unsigned long)
#endif

static long hdma_file_ioctl(struct file *file, unsigned cmd, unsigned long arg);
static int  hdma_file_open(struct inode *inode, struct file *file);
static void alloc_hdma_regions(void);

struct hdmaregion  {
	phys_addr_t start;
	phys_addr_t size;
	bool check_fatal_signals;
	struct {
		phys_addr_t size;
		phys_addr_t start;
	} aligned;
	/* page is returned/used by dma-contiguous API to allocate/release
	   memory from contiguous pool */
	struct page *page;
	unsigned int count;
	struct device *dev;
	struct device dev2;
};

#define MAX_CMA_REGIONS 16

struct cmainfo  {
	int nr_regions;
	struct hdmaregion region[MAX_CMA_REGIONS];
};

struct cmainfo hdmainfo;
static bool dmsg_enabled = 1;

static ssize_t hdma_region_show_start(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

#if defined CONFIG_MIGRATION && defined DEBUG
	{
		struct page *page = phys_to_page(region->aligned.start);
		pr_info("pbmt %d(phys), %d(alloc) cma %d\n",
			get_pageblock_migratetype(page),
			region->page
			? get_pageblock_migratetype(region->page)
			: -1,
			MIGRATE_CMA);
	}
#endif
	return snprintf(buf, 20, "%#llx\n", (u64)region->start);
}

#ifdef DEBUG
static ssize_t hdma_region_show_dstart(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

	return snprintf(buf, 39, "%#llx %#llx\n",
		       (u64)region->start,
		       (u64)region->aligned.start);
}
#endif

#ifdef DEBUG
static ssize_t hdma_region_show_ctl(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	ssize_t ret;
	char *msg;
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

	if (region->page)
		msg = "allocated\n";
	else
		msg = "free\n";
	ret = strlen(msg) + 1;
	memcpy(buf, msg, ret);
	return ret;
}
#endif

static int region_alloc(struct hdmaregion *r, int retry)
{
	unsigned int count = r->aligned.size >> PAGE_SHIFT;

	if (!r->dev2.cma_area)  {
		/* cma_area will not be NULL iff hdma_regions_reserve
		   was invoked and consequently the memory underlying
		   this region was reserved with CMA (early in the
		   boot process and by a platform dependent code) */
		pr_err("cma area for region %d is not set\n",
		       r - hdmainfo.region);
		return -EINVAL;
	}
	if (r->page) {
		pr_info("bad page %p\n", r->page);
		return -EINVAL;
	}
again:
	r->page = dma_alloc_from_contiguous(&r->dev2, count, 0);
	if (!r->page) {
		pr_info_ratelimited("dma_alloc_from_contiguous failed count=%d size=%llu\n",
				    count, (u64)r->aligned.size);
		if (retry)
			if (r->check_fatal_signals
			    && fatal_signal_pending(current))
				return -EINTR;
			else
				goto again;
		else
			return -ENOMEM;
	}
	r->count = count;
	pr_info("region_alloc all good %d@%p(%#llx)\n",
		count, r->page, (u64)page_to_phys(r->page));

	if (page_to_phys(r->page) != r->aligned.start) {
		pr_warn("page physical addresses mismatch %#llx vs %#llx\n",
			(u64)page_to_phys(r->page), (u64)r->aligned.start);
	}
	return 0;
}

static int region_free(struct hdmaregion *r)
{
	if (!r->page)
		return -EINVAL;
	if (r->count <= 0)
		return -EINVAL;
	if (!dma_release_from_contiguous(&r->dev2, r->page, r->count)) {
		pr_debug("dma_release_from_contiguous failed %p %u\n",
			 r->page, r->count);
		return -EINVAL;
	}
	pr_debug("dma_release_from_contiguous succeeded %p %u\n",
		 r->page, r->count);
	r->page = NULL;
	r->count = 0;
	return 0;
}

#ifdef DEBUG
static ssize_t hdma_region_store_ctl(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t n)
{
	int ret;
	struct hdmaregion *r = dev_get_drvdata(dev);

	if (r && n == 1) {
		switch (*buf) {
		case '0':
			ret = region_free(r);
			return ret ? ret : 1;
		case '1':
			ret = region_alloc(r, 0);
			return ret ? ret : 1;
		case '2':
			ret = region_alloc(r, 1);
			return ret ? ret : 1;
		default:
			return -EINVAL;
		}
	} else
		return -EINVAL;
}
#endif

static ssize_t hdma_region_show_cfs(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	ssize_t ret;
	char *msg;
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

	if (region->check_fatal_signals)
		msg = "active\n";
	else
		msg = "not active\n";
	ret = strlen(msg) + 1;
	memcpy(buf, msg, ret);
	return ret;
}

static ssize_t hdma_region_store_cfs(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t n)
{
	struct hdmaregion *r = dev_get_drvdata(dev);

	if (r && n == 1) {
		switch (*buf) {
		case '0':
			r->check_fatal_signals = 0;
			return 1;
		case '1':
			r->check_fatal_signals = 1;
			return 1;
		default:
			return -EINVAL;
		}
	} else
		return -EINVAL;
}

#ifdef DEBUG
static ssize_t hdma_region_show_dmsg(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	ssize_t ret;
	char *msg;
	if (dmsg_enabled)
		msg = "debug message on\n";
	else
		msg = "debug message off\n";
	ret = strlen(msg) + 1;
	memcpy(buf, msg, ret);
	return ret;
}

static ssize_t hdma_region_store_dmsg(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t n)
{
	if (n == 1) {
		switch (*buf) {
		case '0':
			dmsg_enabled = 0;
			return 1;
		case '1':
			dmsg_enabled = 1;
			return 1;
		default:
			return -EINVAL;
		}
	} else
		return -EINVAL;
}
#endif

static ssize_t hdma_region_show_size(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

	return snprintf(buf, 20, "%#llx\n", (u64)region->size);
}

#ifdef DEBUG
static ssize_t hdma_region_show_dsize(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct hdmaregion *region = dev_get_drvdata(dev);

	if (!region)
		return 0;

	return snprintf(buf, 43, "%llu %llu\n",
		       (u64)region->size,
		       (u64)region->aligned.size);
}
#endif

#ifdef DEBUG
static ssize_t hdma_region_show_oom(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	unsigned long addr = __get_free_pages(GFP_KERNEL, 5);
	pr_info("addr=%p\n", (void *) addr);
	if (!addr) {
		return -ENOMEM;
	} else {
		free_page(addr + 4096);
		return 0;
	}
}
#endif

static const struct file_operations hdma_fops = {
	.owner          = THIS_MODULE,
	.open           = hdma_file_open,
	.unlocked_ioctl = hdma_file_ioctl,
};

static struct class *hdma_class;

static struct device_attribute hdma_attrs[] = {
	__ATTR(start, 0444, hdma_region_show_start, NULL),
	__ATTR(size, 0444, hdma_region_show_size, NULL),
	__ATTR(cfs, 0666, hdma_region_show_cfs, hdma_region_store_cfs),
#ifdef DEBUG
	__ATTR(ctl, 0666, hdma_region_show_ctl, hdma_region_store_ctl),
	__ATTR(oom, 0444, hdma_region_show_oom, NULL),
	__ATTR(dstart, 0444, hdma_region_show_dstart, NULL),
	__ATTR(dsize, 0444, hdma_region_show_dsize, NULL),
	__ATTR(dmsg, 0666, hdma_region_show_dmsg, hdma_region_store_dmsg),
#endif
	__ATTR_NULL
};

MODULE_LICENSE("GPL");

static int cmdline_region_index;

static bool range_intersects(phys_addr_t s1, phys_addr_t e1,
			     phys_addr_t s2, phys_addr_t e2)
{
	return !(e2 <= s1 || s2 >= e1);
}

static bool range_contains(phys_addr_t s1, phys_addr_t e1,
			   phys_addr_t s2, phys_addr_t e2)
{
	return s1 >= s2 && e1 <= e2;
}

static int __init hdma_add_region(phys_addr_t start, phys_addr_t size)
{
	int i;
	phys_addr_t t1, t2;
	phys_addr_t alignment, start1, end1;
	struct hdmaregion *region = &hdmainfo.region[hdmainfo.nr_regions];
	

	if (hdmainfo.nr_regions >= MAX_CMA_REGIONS) {
		pr_crit("MAX_CMA_REGIONS too low, ignoring memory %#llx@0x%#llx\n",
			(u64)start, (u64)size);
		return -EINVAL;
	}


	/*
	 * Ensure that start/size are aligned to a page boundary.
	 * Start is appropriately rounded down, size is rounded up.
	 */
	size += start & ~PAGE_MASK;
	region->start = start;

	/* band aid that must be in sync with alignment games
	 * dma-contiguous plays */
	alignment = PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);
	/* Additional alignment requirement for dma_contiguous_remap */
	if (alignment < PMD_SIZE)
		alignment = PMD_SIZE;
	region->aligned.start = region->start & ~(alignment-1);
	pr_info("start=%#llx aligned_start=%#llx\n",
		region->start + 0ULL, region->aligned.start + 0ULL);
	region->size = size;
	region->aligned.size = size + (region->start - region->aligned.start);
	region->aligned.size += alignment - 1;
	region->aligned.size &= ~(alignment - 1);

	start1 = region->start;
	t1 = region->start - region->aligned.start;
	t2 = region->aligned.size - size;
	pr_debug("size=%llu vs %llu (overhead %llu, %llu)\n",
		 (u64)size, (u64)region->aligned.size, (u64)t1, (u64)t2);
	end1 = region->aligned.start + region->aligned.size;
	if (end1 <= region->aligned.start) {
		pr_err("invalid region (overflow) (start=%#llx, size=%llu, end=%#llx)\n",
		       (u64)start1, (u64)region->aligned.size, (u64)end1);
		return -EINVAL;
	}

	/*
	 * Check whether this memory region has non-zero size or
	 * invalid node number.
	 */
	if (region->aligned.size == 0)
		return -EINVAL;

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		phys_addr_t start2, end2;
		struct hdmaregion *r2 = &hdmainfo.region[i];

		start2 = r2->aligned.start;
		end2 = start2 + r2->aligned.size;

		if (range_intersects(start1, end1,
				     start2, end2))  {
			pr_err("overlapping regions found %d %d [%#llx..%#llx] [%#llx..%#llx]\n",
			       i, cmdline_region_index,
			       (u64)start1, (u64)end1,
			       (u64)start2, (u64)end2);
			return -EINVAL;
		}
	}

	for (i = 0; i < meminfo.nr_banks; ++i) {
		if (range_contains(start1, end1,
				   meminfo.bank[i].start,
				   meminfo.bank[i].start +
				   meminfo.bank[i].size))
			goto found;
	}
	pr_err("region range(%d) [%#llx, %#llx] is not found in any of the memory blocks\n",
	       cmdline_region_index, (u64)start1, (u64)end1);
	return -EINVAL;

found:
	pr_info("added region %d %#llx %llu\n", hdmainfo.nr_regions,
		(u64)region->start, (u64)region->size);
	hdmainfo.nr_regions++;
	return 0;
}

/*
 * Pick out the HDMA size and offset.  We look for hdma=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init early_hdma(char *p)
{
	phys_addr_t size, start = 0;
	char *endp;

	size  = memparse(p, &endp);
	if (*endp != '@') {
		pr_err("hdma cmdline parameter [%s] skipped. Check parameter form [size@offt]\n",
		       p);
		return 0;
	}
	start = memparse(endp + 1, NULL);
	hdma_add_region(start, size);
	cmdline_region_index += 1;

	return 0;
}
early_param("hdma", early_hdma);

static int add_hdma_region(struct hdmaregion *region)
{
	int ret = 0, id = region - hdmainfo.region;

	region->dev = device_create(hdma_class, NULL,
				    MKDEV(HDMA_CHAR_MAJOR, id), region,
				    "hdma%d", id);
	dev_info(region->dev, "device_create %p\n", region->dev);
	if (unlikely(IS_ERR(region->dev))) {
		ret = PTR_ERR(region->dev);
		goto out_device;
	}

	dev_set_drvdata(region->dev, region);

	dev_info(region->dev, "region added size=%llu start=%#llx\n",
		 (u64)region->size, (u64)region->start);

	return ret;

 out_device:
	return ret;
}

static void add_hdma_regions(void)
{
	int i;

	for (i = 0; i < hdmainfo.nr_regions; ++i)
		add_hdma_region(&hdmainfo.region[i]);
}

static int __init hdma_dev_init(void)
{
	int i, ret;
	struct zone *zone;

	pr_info("hdma_dev_init\n");

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		struct hdmaregion *region = &hdmainfo.region[i];
		phys_addr_t rstart, rend, zsize;
		rstart = region->aligned.start;
		rend = rstart + region->aligned.size;
		pr_info("testing region %d [%#llx, %#llx]\n", i,
			(u64)rstart, (u64)rend);
		for_each_zone(zone) {
			phys_addr_t zstart, zend;

			zstart = zone->zone_start_pfn;
			zsize = zone->spanned_pages;
			zstart <<= PAGE_SHIFT;
			zsize <<= PAGE_SHIFT;
			zend = zstart + zsize;
#ifdef DEBUG
			pr_info("zone %-11s [%#llx, %#llx]\n",
				zone->name, (u64)zstart, (u64)zend);
#endif
			if (range_contains(rstart, rend, zstart, zend))
				goto found;
		}
		pr_err("region [%#llx, %#llx] was not found in any of the memory zones\n",
		       (u64)rstart, (u64)rend);
		region->aligned.start = 0;
found:;
	}

rerun:
	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		struct hdmaregion *region = &hdmainfo.region[i];
		if (region->aligned.start == 0) {
			if (i != hdmainfo.nr_regions - 1)
				memcpy(region, region + 1, sizeof(*region));
			hdmainfo.nr_regions -= 1;
			goto rerun;
		}
	}

	ret = register_chrdev(HDMA_CHAR_MAJOR, HDMA_DEV_NAME, &hdma_fops);
	if (ret)
		goto out;
	hdma_class = class_create(THIS_MODULE, HDMA_DEV_NAME);
	if (IS_ERR(hdma_class)) {
		ret = PTR_ERR(hdma_class);
		goto out_unreg_chrdev;
	}
	hdma_class->dev_attrs = hdma_attrs;
	add_hdma_regions();
	alloc_hdma_regions();
	return 0;
 out_unreg_chrdev:
	unregister_chrdev(HDMA_CHAR_MAJOR, HDMA_DEV_NAME);
 out:
	pr_debug("registration failed: %d\n", ret);
	return ret;
}
module_init(hdma_dev_init);

void __init hdma_regions_reserve(void)
{
	int i, err;
	struct hdmaregion *region;

	pr_info("hdma_regions_reserve %d defined\n",
		hdmainfo.nr_regions);
	for (i = 0; i < hdmainfo.nr_regions; i++) {
		region = &hdmainfo.region[i];
		err = dma_declare_contiguous(&region->dev2,
					     region->aligned.size,
					     region->aligned.start,
					     0);
		pr_info("dma_declare_contiguous[%d] %llu@%#llx = %d\n",
			i, (u64)region->aligned.size,
			(u64)region->aligned.start,
			err);
		if (err) {
			pr_warn("unable to reserve %llu for CMA: at %#llx\n",
				(u64)region->aligned.size,
				(u64)region->aligned.start);
			region->aligned.start = 0;
		}
	}
}

static int hdma_file_open(struct inode *inode, struct file *file)
{
	struct hdmaregion *region;
	int minor = iminor(inode);

	pr_warn("file open minor %d\n", minor);
	region = &hdmainfo.region[minor];

	if (!region)
		return -ENODEV;

	dev_dbg(region->dev, "%s(%p)\n", __func__, (void *) file);

	file->private_data = region;
	pr_warn("file open success: minor %d region %p\n",
		minor, region);
	return 0;
}

DEFINE_MUTEX(hdma_ioctl_mutex);

extern void _pt_zram_info(struct seq_file *m);

#ifdef CONFIG_VD_MEMINFO
struct mem_cgroup;
extern void dump_tasks_plus(const struct mem_cgroup *mem, struct seq_file *s);
#endif

static void free_hdma_regions(void)
{
	int i;

 try_again:
	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		int ret;
		struct hdmaregion *r = &hdmainfo.region[i];

		if (r->page) {
			ret = region_free(r);
			if (ret) {
				if (ret == -ENOMEM)
					goto try_again;
				else
					break;
			} else
				pr_info("VDLinux HDMA msg, HDMA_IS_ON on region %d\n",
				i);
		}
	}
	if (dmsg_enabled) {
		show_free_areas(0);
		_pt_zram_info(NULL);
	}
}

static void alloc_hdma_regions(void)
{
	int i;
	pr_info("VDLinux HDMA msg, HDMA_IS_MIGRATING.\n");
	if (dmsg_enabled) {
		show_free_areas(0);
		_pt_zram_info(NULL);
#ifdef CONFIV_VD_MEMINFO
		dump_tasks_plus(NULL, NULL);
#endif
	}

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		int ret;
		struct hdmaregion *r = &hdmainfo.region[i];

		if (!r->page) {
			ret = region_alloc(r, 1);
			if (ret)
				pr_err("region_alloc(, 1) returned %d\n", ret);
			else
				pr_info("VDLinux HDMA msg, HDMA_IS_OFF on region %d\n",
				i);
		}
	}
	if (dmsg_enabled) {
		show_free_areas(0);
		_pt_zram_info(NULL);
	}
}

#ifdef CONFIG_CMA_APP_ALLOC
static bool hdma_process_set_alloc(struct task_struct *task, bool alloc)
{
	struct task_struct *p = task;

	read_lock(&tasklist_lock);

	/* check that while_each_thread is safe */
	if (!pid_alive(task)) {
		read_unlock(&tasklist_lock);
		return false;
	}

	/* modify the flag for the whole thread group */
	do {
		p->cma_alloc = alloc;
	} while_each_thread(task, p);

	read_unlock(&tasklist_lock);

	return true;
}

static long hdma_file_ioctl_alloc(pid_t pid, bool cma_alloc)
{
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	rcu_read_unlock();

	if (!task)
		return -EINVAL;

	if (!hdma_process_set_alloc(task, cma_alloc))
		return -EINVAL;

	return 0;
}
#endif

static long hdma_file_ioctl_notify(unsigned long arg)
{
	int ret = 0;

	if (hdmainfo.nr_regions == 0)
		return -ENOMEM;

	mutex_lock(&hdma_ioctl_mutex);

	pr_info("hdma_file_ioctl_notify %lu\n", arg);
	switch (arg) {
	case 0: /* notify-free */
		free_hdma_regions();
		break;
	case 1: /* notify-alloc */
		alloc_hdma_regions();
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&hdma_ioctl_mutex);
	return ret;
}

static long hdma_file_ioctl_notify_region(struct hdmaregion *region,
					  unsigned long arg)
{
	int ret = 0;

	mutex_lock(&hdma_ioctl_mutex);
	pr_info("hdma_file_ioctl_notify_region %lu %d\n",
		arg, region - hdmainfo.region);
	switch (arg) {
	case 0: /* notify-free */
		ret = region_free(region);
		/* ret = region->size >> PAGE_SHIFT; */
		break;
	case 1: /* notify-alloc */
		ret = region_alloc(region, 0);
		break;
	case 2: /* notify-alloc-do-pobednogo */
		ret = region_alloc(region, 1);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&hdma_ioctl_mutex);
	return ret;
}

static long hdma_file_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct hdmaregion *region = file->private_data;

	if (!region)
		return -EBADFD;

	switch (cmd) {
	case IOCTL_HDMA_NOTIFY_ALL:
		return hdma_file_ioctl_notify(arg);

	case IOCTL_HDMA_NOTIFY:
		return hdma_file_ioctl_notify_region(region, arg);

#ifdef CONFIG_CMA_APP_ALLOC
	case IOCTL_HDMA_ALLOC_SET:
		return hdma_file_ioctl_alloc(arg, true);

	case IOCTL_HDMA_ALLOC_CLEAR:
		return hdma_file_ioctl_alloc(arg, false);
#endif

	default:
		return -ENOTTY;
	}
}

int hdma_is_page_reserved(struct page *page)
{
	unsigned i;
	phys_addr_t phys = page_to_phys(page);

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		struct hdmaregion *r = &hdmainfo.region[i];

		if (phys >= r->aligned.start
		    && phys < r->aligned.start + r->aligned.size)
			return 1;
	}
	return 0;
}

phys_addr_t hdma_declared_pages(void)
{
	unsigned i;
	phys_addr_t size = 0;

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		struct hdmaregion *r = &hdmainfo.region[i];

		size += r->aligned.size >> PAGE_SHIFT;
	}
	return size;
}

phys_addr_t hdma_allocated_pages(void)
{
	unsigned i;
	phys_addr_t size = 0;

	for (i = 0; i < hdmainfo.nr_regions; ++i) {
		struct hdmaregion *r = &hdmainfo.region[i];

		if (r->page)
			size += r->aligned.size >> PAGE_SHIFT;
	}
	return size;
}
