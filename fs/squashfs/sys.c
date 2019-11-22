#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"

enum squashfs_hw_decompressor hw_decompressor =
#if   defined(CONFIG_SQUASHFS_HW2)
	HW2_DECOMPRESSOR;
#elif defined(CONFIG_SQUASHFS_HW1)
	HW1_DECOMPRESSOR;
#else
	SW_DECOMPRESSOR;
#endif

static struct {
	enum squashfs_hw_decompressor hw_type;
	char			     *hw_name;
} hw_arr[] = {
	{ SW_DECOMPRESSOR,  "sw"  },
#ifdef CONFIG_SQUASHFS_HW1
	{ HW1_DECOMPRESSOR, "hw1" },
#endif
#ifdef CONFIG_SQUASHFS_HW2
	{ HW2_DECOMPRESSOR, "hw2" },
#endif
};

static struct kset       *sqfs_kset;
static struct kobject     sqfs_opt_kobj;
static struct completion  sqfs_kobj_unregister;

struct sqfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct sqfs_attr *, char *);
	ssize_t (*store)(struct sqfs_attr *, const char *, size_t);
};

static ssize_t hw_decompressor_show(struct sqfs_attr *a, char *page)
{
	int i;
	enum squashfs_hw_decompressor hw_type;
	char *hw_name;
	int len = 0;
	char b[16];

	for (i = 0; i < ARRAY_SIZE(hw_arr); ++i) {
		hw_type = hw_arr[i].hw_type;
		hw_name = hw_arr[i].hw_name;

		if (hw_decompressor == hw_type)
			len += snprintf(b + len, sizeof(b) - len, "%s[%s]",
					i ? " " : "", hw_name);
		else
			len += snprintf(b + len, sizeof(b) - len, "%s%s",
					i ? " " : "", hw_name);
	}
	return snprintf(page, PAGE_SIZE, "%s\n", b);
}

static ssize_t hw_decompressor_store(struct sqfs_attr *a, const char *page,
				     size_t len)
{
	int i;
	enum squashfs_hw_decompressor hw_type;
	char *hw_name;
	for (i = 0; i < ARRAY_SIZE(hw_arr); ++i) {
		hw_type = hw_arr[i].hw_type;
		hw_name = hw_arr[i].hw_name;

		if (!strncmp(page, hw_name, strlen(hw_name))) {
			hw_decompressor = hw_type;
			break;
		}
	}

	return len;
}

static ssize_t sqfs_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *page)
{
	struct sqfs_attr *a = container_of(attr, struct sqfs_attr, attr);

	return a->show ? a->show(a, page) : 0;
}

static ssize_t sqfs_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	struct sqfs_attr *a = container_of(attr, struct sqfs_attr, attr);

	return a->store ? a->store(a, buf, len) : len;
}

static const struct sysfs_ops sqfs_attr_ops = {
	.show  = sqfs_attr_show,
	.store = sqfs_attr_store,
};

#define SQFS_ATTR(name, mode, show, store)	   \
	static struct sqfs_attr sqfs_attr_##name = \
		__ATTR(name, mode, show, store)

SQFS_ATTR(hw_decompressor, S_IRUGO | S_IWUSR,
	  hw_decompressor_show, hw_decompressor_store);

static struct attribute *sqfs_opt_attrs[] = {
	&sqfs_attr_hw_decompressor.attr,
	NULL,
};

static void sqfs_attr_release(struct kobject *kobj)
{
	complete(&sqfs_kobj_unregister);
}

static struct kobj_type sqfs_opt_ktype = {
	.default_attrs	= sqfs_opt_attrs,
	.sysfs_ops	= &sqfs_attr_ops,
	.release	= sqfs_attr_release
};

int squashfs_sysfs_init(void)
{
	int ret;

	init_completion(&sqfs_kobj_unregister);

	sqfs_kset = kset_create_and_add("squashfs", NULL, fs_kobj);
	if (!sqfs_kset) {
		ERROR("kset_create_and_add squashfs failed\n");
		return -ENOMEM;
	}

	ret = kobject_init_and_add(&sqfs_opt_kobj, &sqfs_opt_ktype,
				   &sqfs_kset->kobj,  "options");
	if (ret) {
		ERROR("Failed to create sysfs\n");
		kset_unregister(sqfs_kset);
		return -ENOMEM;
	}

	return 0;
}

void squashfs_sysfs_cleanup(void)
{
	kobject_del(&sqfs_opt_kobj);
	kobject_put(&sqfs_opt_kobj);
	wait_for_completion(&sqfs_kobj_unregister);
	kset_unregister(sqfs_kset);
}
