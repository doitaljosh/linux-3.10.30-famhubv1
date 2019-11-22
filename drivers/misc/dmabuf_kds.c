/*
 *
 * (C) COPYRIGHT 2013 Samsung Electronics. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/kds.h>
#include <linux/dma-buf.h>
#include <drm/drmP.h>

static struct dmabuf_kds_dev {
	struct class *class;
	struct device *device;
} dmabuf_kds_dev;

enum dmabuf_kds_ioctls {
	DMABUF_KDS_WAIT,
	DMABUF_KDS_RELEASE,
};

struct dmabuf_kds_arg {
	int drm_fd;
	unsigned int gem_handle;
	void *lock;
};

#define DMABUF_KDS_MAJOR 225

#define DMABUF_KDS_CMD_WAIT		_IOWR('K', DMABUF_KDS_WAIT,	\
					struct dmabuf_kds_arg *)
#define DMABUF_KDS_CMD_RELEASE		_IOW('K', DMABUF_KDS_RELEASE,	\
					struct kds_resource_set *)

static const char dmabuf_kds_dev_name[] = "dmabuf_kds";


static long dmabuf_kds_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	long ret = 0;
	struct dmabuf_kds_arg *kds_arg;
	struct file *filp;
	struct drm_file *drm_file;
	struct drm_gem_object *gem_obj;
	struct dma_buf *dma_buf;
	struct kds_resource *kds_res;
	struct kds_resource *kds_resources[1];
	struct kds_resource_set *lock;
	unsigned long exclusive[1] = {1};

	switch (cmd) {
	case DMABUF_KDS_CMD_WAIT:
		kds_arg = (struct dmabuf_kds_arg *)arg;
		kds_arg->lock = NULL;

		filp = fcheck(kds_arg->drm_fd);
		if (filp == NULL) {
			return -EINVAL;
		}

		drm_file = filp->private_data;

		gem_obj = drm_gem_object_lookup(NULL, drm_file,
				kds_arg->gem_handle);
		if (gem_obj == NULL) {
			return -EINVAL;
		}

		dma_buf = gem_obj->export_dma_buf;
		if (dma_buf == NULL) {
			drm_gem_object_unreference(gem_obj);
			return -ENOENT;
		}

		get_dma_buf(dma_buf);

		kds_res = get_dma_buf_kds_resource(dma_buf);
		if (kds_res) {
			kds_resources[0] = kds_res;

			lock = kds_waitall(1, exclusive, kds_resources, HZ);
			if (IS_ERR(lock)) {
				printk("[%s:%d] error in kds_waitall: %d (%d)\n",
					__func__, __LINE__,
					kds_arg->gem_handle, PTR_RET(lock));
				ret = PTR_RET(lock);
			} else {
				if (lock)
					kds_arg->lock = lock;
			}
		}

		dma_buf_put(dma_buf);
		drm_gem_object_unreference(gem_obj);

		break;
	case DMABUF_KDS_CMD_RELEASE:
		lock = (struct kds_resource_set *)arg;
		kds_resource_set_release(&lock);

		break;
	default:
		printk("[%s:%d] Unknown ioctl cmd\n", __func__, __LINE__);
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations dmabuf_kds_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dmabuf_kds_ioctl,
};

int dmabuf_kds_init(void)
{
	int ret;

	ret = register_chrdev(DMABUF_KDS_MAJOR, dmabuf_kds_dev_name, &dmabuf_kds_ops);
	if (ret < 0) {
		printk("failed to register dmabuf_kds device\n");
		return ret;
	}

	dmabuf_kds_dev.class = class_create(THIS_MODULE, dmabuf_kds_dev_name);
	if (IS_ERR(dmabuf_kds_dev.class)) {
		unregister_chrdev(DMABUF_KDS_MAJOR, dmabuf_kds_dev_name);
		printk("failed to create class for the dmabuf_kds\n");
		return -1;
	}

	dmabuf_kds_dev.device = device_create(dmabuf_kds_dev.class, NULL,
			MKDEV(DMABUF_KDS_MAJOR, 0), NULL, dmabuf_kds_dev_name);
	if (IS_ERR(dmabuf_kds_dev.device)) {
		unregister_chrdev(DMABUF_KDS_MAJOR, dmabuf_kds_dev_name);
		class_unregister(dmabuf_kds_dev.class);
		printk("failed to create dmabuf_kds device");
		return -1;
	}

	return 0;
}

void dmabuf_kds_exit(void)
{
	device_destroy(dmabuf_kds_dev.class, MKDEV(DMABUF_KDS_MAJOR, 0));
	class_destroy(dmabuf_kds_dev.class);
	unregister_chrdev(DMABUF_KDS_MAJOR, dmabuf_kds_dev_name);
}

module_init(dmabuf_kds_init);
module_exit(dmabuf_kds_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samsung Electronics");
MODULE_VERSION("0.1");
