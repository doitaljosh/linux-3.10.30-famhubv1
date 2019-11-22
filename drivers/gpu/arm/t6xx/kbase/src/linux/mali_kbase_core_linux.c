/*
 *
 * (C) COPYRIGHT 2010-2013 ARM Limited. All rights reserved.
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



/**
 * @file mali_kbase_core_linux.c
 * Base kernel driver init.
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>
#include <kbase/src/linux/mali_kbase_config_linux.h>
#include <kbase/src/common/mali_kbase_pm.h>

#ifdef CONFIG_MALI_NO_MALI
#include "mali_kbase_model_linux.h"
#endif				/* CONFIG_MALI_NO_MALI */

#ifdef CONFIG_KDS
#include <linux/kds.h>
#include <linux/anon_inodes.h>
#include <linux/syscalls.h>
#endif				/* CONFIG_KDS */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/compat.h>	/* is_compat_task */
#include <kbase/src/common/mali_kbase_8401_workaround.h>
#include <kbase/src/common/mali_kbase_hw.h>
#include <kbase/src/common/mali_kbase_pm.h>

#ifdef CONFIG_SYNC
#include <kbase/src/linux/mali_kbase_sync.h>
#endif				/* CONFIG_SYNC */

#if 1
extern int kds_initialize_dma_buf(void);	

#if CONFIG_DMABUF_KDS_LOCK 	
#include <linux/dma-buf.h>	
extern int dmabuf_kds_init(void);
extern void dmabuf_kds_exit(void);	
#endif	

#endif

#ifdef CONFIG_MACH_MANTA
#include <plat/devs.h>
#endif

#define	JOB_IRQ_TAG	0
#define MMU_IRQ_TAG	1
#define GPU_IRQ_TAG	2

/* DVFS : reina*/
extern bool g_bASV_OnOFF;
extern bool g_bDVFS_Print;
extern bool g_bThermal_limit;
extern kbase_pm_dvfs_status g_DVFS_OnOFF;
extern unsigned int g_u32DVFS_CurLevel;
extern unsigned int g_u32DVFS_Manual_Level;
extern unsigned int g_u32DVFS_FixLevel; 
extern unsigned int max_support_idx;
extern unsigned int min_support_idx;
extern unsigned int cpu_lock_threshold;
extern unsigned int cpu_unlock_threshold;
extern unsigned int max_real_idx;
extern unsigned int gpu_result_of_asv;
extern unsigned int GolfP_Volt_table[GPUFREQ_LEVEL_END];
extern struct gpufreq_frequency_table GolfP_freq_table[];
/* W0000135754 - for emergency Thermal test */
extern bool g_Thermal_limit_50;
extern void dvfs_set_min_frequency(u32 min_freq);
extern void dvfs_set_max_frequency(u32 max_freq);
void kbase_get_mem_info(struct kbase_context *);


extern int sdp_ccepfb_init(void);
extern void sdp_ccepfb_cleanup(void);
extern int umpp_linux_initialize_module(void);
extern void umpp_linux_cleanup_module(void);

extern int omap_drm_init(void);
extern void omap_drm_fini(void);

extern int dma_buf_lock_init(void);
extern void dma_buf_lock_exit(void);

struct kbase_irq_table {
	u32 tag;
	irq_handler_t handler;
};
#if MALI_UNIT_TEST
kbase_exported_test_data shared_kernel_test_data;
EXPORT_SYMBOL(shared_kernel_test_data);
#endif				/* MALI_UNIT_TEST */

#define KBASE_DRV_NAME "mali"

static const char kbase_drv_name[] = KBASE_DRV_NAME;

static int kbase_dev_nr;

static DEFINE_SEMAPHORE(kbase_dev_list_lock);
static LIST_HEAD(kbase_dev_list);

KBASE_EXPORT_TEST_API(kbase_dev_list_lock)
KBASE_EXPORT_TEST_API(kbase_dev_list)
#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"
static INLINE void __compile_time_asserts(void)
{
	CSTD_COMPILE_TIME_ASSERT(sizeof(KERNEL_SIDE_DDK_VERSION_STRING) <= KBASE_GET_VERSION_BUFFER_SIZE);
}

#ifdef CONFIG_KDS

typedef struct kbasep_kds_resource_set_file_data {
	struct kds_resource_set *lock;
} kbasep_kds_resource_set_file_data;

static int kds_resource_release(struct inode *inode, struct file *file);

static const struct file_operations kds_resource_fops = {
	.release = kds_resource_release
};

typedef struct kbase_kds_resource_list_data {
	struct kds_resource **kds_resources;
	unsigned long *kds_access_bitmap;
	int num_elems;
} kbase_kds_resource_list_data;

static int kds_resource_release(struct inode *inode, struct file *file)
{
	struct kbasep_kds_resource_set_file_data *data;

	data = (struct kbasep_kds_resource_set_file_data *)file->private_data;
	if (NULL != data) {
		if (NULL != data->lock)
			kds_resource_set_release(&data->lock);

		kfree(data);
	}
	return 0;
}

mali_error kbasep_kds_allocate_resource_list_data(kbase_context *kctx, base_external_resource *ext_res, int num_elems, kbase_kds_resource_list_data *resources_list)
{
	base_external_resource *res = ext_res;
	int res_id;

	/* assume we have to wait for all */

	KBASE_DEBUG_ASSERT(0 != num_elems);
	resources_list->kds_resources = kmalloc(sizeof(struct kds_resource *) * num_elems, GFP_KERNEL);

	if (NULL == resources_list->kds_resources)
		return MALI_ERROR_OUT_OF_MEMORY;

	KBASE_DEBUG_ASSERT(0 != num_elems);
	resources_list->kds_access_bitmap = kzalloc(sizeof(unsigned long) * ((num_elems + BITS_PER_LONG - 1) / BITS_PER_LONG), GFP_KERNEL);

	if (NULL == resources_list->kds_access_bitmap) {
	    kfree(resources_list->kds_resources);
	    kfree(resources_list->kds_access_bitmap);
	    return MALI_ERROR_OUT_OF_MEMORY;
	}

	for (res_id = 0; res_id < num_elems; res_id++, res++) {
		int exclusive;
		kbase_va_region *reg;
		struct kds_resource *kds_res = NULL;

		exclusive = res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE;
		reg = kbase_region_tracker_find_region_enclosing_address(kctx, res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);

		/* did we find a matching region object? */
		if (NULL == reg)
			break;

		switch (reg->imported_type) {
#if defined(CONFIG_UMP) && defined(CONFIG_KDS)
		case BASE_TMEM_IMPORT_TYPE_UMP:
			kds_res = ump_dd_kds_resource_get(reg->imported_metadata.ump_handle);
			break;
#endif				/* defined(CONFIG_UMP) && defined(CONFIG_KDS) */
		default:
			break;
		}

		/* no kds resource for the region ? */
		if (!kds_res)
			break;

		resources_list->kds_resources[res_id] = kds_res;

		if (exclusive)
			set_bit(res_id, resources_list->kds_access_bitmap);
	}

	/* did the loop run to completion? */
	if (res_id == num_elems)
		return MALI_ERROR_NONE;

	/* Clean up as the resource list is not valid. */
	kfree(resources_list->kds_resources);
	kfree(resources_list->kds_access_bitmap);

	return MALI_ERROR_FUNCTION_FAILED;
}

mali_bool kbasep_validate_kbase_pointer(kbase_pointer *p)
{
#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		if (p->compat_value == 0)
			return MALI_FALSE;
	} else {
#endif				/* CONFIG_COMPAT */
		if (NULL == p->value)
			return MALI_FALSE;
#ifdef CONFIG_COMPAT
	}
#endif				/* CONFIG_COMPAT */
	return MALI_TRUE;
}

mali_error kbase_external_buffer_lock(kbase_context *kctx, kbase_uk_ext_buff_kds_data *args, u32 args_size)
{
	base_external_resource *ext_res_copy;
	size_t ext_resource_size;
	mali_error return_error = MALI_ERROR_FUNCTION_FAILED;
	int fd;

	if (args_size != sizeof(kbase_uk_ext_buff_kds_data))
		return MALI_ERROR_FUNCTION_FAILED;

	/* Check user space has provided valid data */
	if (!kbasep_validate_kbase_pointer(&args->external_resource) || !kbasep_validate_kbase_pointer(&args->file_descriptor) || (0 == args->num_res) || (args->num_res > KBASE_MAXIMUM_EXT_RESOURCES))
		return MALI_ERROR_FUNCTION_FAILED;

	ext_resource_size = sizeof(base_external_resource) * args->num_res;

	KBASE_DEBUG_ASSERT(0 != ext_resource_size);
	ext_res_copy = kmalloc(ext_resource_size, GFP_KERNEL);

	if (NULL != ext_res_copy) {
		base_external_resource *__user ext_res_user;
		int *__user file_descriptor_user;
#ifdef CONFIG_COMPAT
		if (is_compat_task()) {
			ext_res_user = args->external_resource.compat_value;
			file_descriptor_user = args->file_descriptor.compat_value;
		} else {
#endif				/* CONFIG_COMPAT */
			ext_res_user = args->external_resource.value;
			file_descriptor_user = args->file_descriptor.value;
#ifdef CONFIG_COMPAT
		}
#endif				/* CONFIG_COMPAT */

		/* Copy the external resources to lock from user space */
		if (0 == copy_from_user(ext_res_copy, ext_res_user, ext_resource_size)) {
			kbasep_kds_resource_set_file_data *fdata;

			/* Allocate data to be stored in the file */
			fdata = kmalloc(sizeof(kbasep_kds_resource_set_file_data), GFP_KERNEL);

			if (NULL != fdata) {
				kbase_kds_resource_list_data resource_list_data;
				/* Parse given elements and create resource and access lists */
				return_error = kbasep_kds_allocate_resource_list_data(kctx, ext_res_copy, args->num_res, &resource_list_data);
				if (MALI_ERROR_NONE == return_error) {
					long err;

					fdata->lock = NULL;

					fd = anon_inode_getfd("kds_ext", &kds_resource_fops, fdata, 0);

					err = copy_to_user(file_descriptor_user, &fd, sizeof(fd));

					/* If the file descriptor was valid and we successfully copied it to user space, then we
					 * can try and lock the requested kds resources.
					 */
					if ((fd >= 0) && (0 == err)) {
						struct kds_resource_set *lock;

						lock = kds_waitall(args->num_res, resource_list_data.kds_access_bitmap, resource_list_data.kds_resources, KDS_WAIT_BLOCKING);

						if (IS_ERR_OR_NULL(lock)) {
							return_error = MALI_ERROR_FUNCTION_FAILED;
						} else {
							return_error = MALI_ERROR_NONE;
							fdata->lock = lock;
						}
					} else {
						return_error = MALI_ERROR_FUNCTION_FAILED;
					}

					kfree(resource_list_data.kds_resources);
					kfree(resource_list_data.kds_access_bitmap);
				}

				if (MALI_ERROR_NONE != return_error) {
					/* If the file was opened successfully then close it which will clean up
					 * the file data, otherwise we clean up the file data ourself. */
					if (fd >= 0)
						sys_close(fd);
					else
						kfree(fdata);
				}
			} else {
				return_error = MALI_ERROR_OUT_OF_MEMORY;
			}
		}
		kfree(ext_res_copy);
	}
	return return_error;
}
#endif				/* CONFIG_KDS */
void kbase_get_mem_info(struct kbase_context *kctx)
{
		u32 max=0, used=0,free=0;
		struct list_head *entry;

		down(&kbase_dev_list_lock);
		list_for_each(entry, &kbase_dev_list) {
				struct kbase_device *kbdev = NULL;
				kbdev = list_entry(entry, struct kbase_device, osdev.entry);
				max = kbdev->memdev.usage.max_pages;
				used = atomic_read(&(kbdev->memdev.usage.cur_pages));
				free = max - used;
		}
		up(&kbase_dev_list_lock);


		KBASE_DEBUG_PRINT(KBASE_MEM, "\n++++++++++++++++++++++++++++++++++++++++++\n");
		KBASE_DEBUG_PRINT(KBASE_MEM, "\n    MALI MEM USAGE\n");
		KBASE_DEBUG_PRINT(KBASE_MEM, "\n++++++++++++++++++++++++++++++++++++++++++\n");
		KBASE_DEBUG_PRINT(KBASE_MEM, "Max Memory Size = %d KB\n",(max*4));
		KBASE_DEBUG_PRINT(KBASE_MEM, "Used Memory Size = %d KB\n",(used*4));
		KBASE_DEBUG_PRINT(KBASE_MEM, "Free Memory Size = %d KB\n",(free*4));
		KBASE_DEBUG_PRINT(KBASE_MEM, "\n++++++++++++++++++++++++++++++++++++++++++\n");
}

static mali_error kbase_dispatch(kbase_context *kctx, void * const args, u32 args_size)
{
	struct kbase_device *kbdev;
	uk_header *ukh = args;
	u32 id;

	KBASE_DEBUG_ASSERT(ukh != NULL);

	kbdev = kctx->kbdev;
	id = ukh->id;
	ukh->ret = MALI_ERROR_NONE;	/* Be optimistic */

	if (UKP_FUNC_ID_CHECK_VERSION == id) {
		if (args_size == sizeof(uku_version_check_args)) {
			uku_version_check_args *version_check = (uku_version_check_args *)args;

			version_check->major = BASE_UK_VERSION_MAJOR;
			version_check->minor = BASE_UK_VERSION_MINOR;

			ukh->ret = MALI_ERROR_NONE;
		} else {
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
		}
		return MALI_ERROR_NONE;
	}


	if (!atomic_read(&kctx->setup_complete)) {
		/* setup pending, try to signal that we'll do the setup */
		if (atomic_cmpxchg(&kctx->setup_in_progress, 0, 1)) {
			/* setup was already in progress, err this call */
			return MALI_ERROR_FUNCTION_FAILED;
		}

		/* we're the one doing setup */

		/* is it the only call we accept? */
		if (id == KBASE_FUNC_SET_FLAGS) {
			kbase_uk_set_flags *kbase_set_flags = (kbase_uk_set_flags *) args;

			if (sizeof(*kbase_set_flags) != args_size) {
				/* not matching the expected call, stay stuck in setup mode */
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_context_set_create_flags(kctx, kbase_set_flags->create_flags)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				/* bad flags, will stay stuck in setup mode */
				return MALI_ERROR_NONE;
			} else {
				/* we've done the setup, all OK */
				atomic_set(&kctx->setup_complete, 1);
				return MALI_ERROR_NONE;
			}
		} else {
			/* unexpected call, will stay stuck in setup mode */
			return MALI_ERROR_FUNCTION_FAILED;
		}
	}

	/* setup complete, perform normal operation */
	switch (id) {
	case KBASE_FUNC_TMEM_ALLOC:
		{
			kbase_uk_tmem_alloc *tmem = args;
			struct kbase_va_region *reg;

			if (sizeof(*tmem) != args_size)
				goto bad_size;

			reg = kbase_tmem_alloc(kctx, tmem->vsize, tmem->psize, tmem->extent, tmem->flags, tmem->is_growable);
			if (reg)
				tmem->gpu_addr = reg->start_pfn << PAGE_SHIFT;
			else
			{
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "%s %d => OOM Issue kbase_tmem_alloc Failed tmem->vsize [%d] tmem->psize[%d] tmem->extent[%d]\n", __FILE__, __LINE__,tmem->vsize, tmem->psize, tmem->extent );
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

	case KBASE_FUNC_TMEM_IMPORT:
		{
			kbase_uk_tmem_import *tmem_import = args;
			struct kbase_va_region *reg;
			int *__user phandle;
			int handle;

			if (sizeof(*tmem_import) != args_size)
				goto bad_size;
#ifdef CONFIG_COMPAT
			if (is_compat_task()) {
				phandle = tmem_import->phandle.compat_value;
			} else {
#endif				/* CONFIG_COMPAT */
				phandle = tmem_import->phandle.value;
#ifdef CONFIG_COMPAT
			}
#endif				/* CONFIG_COMPAT */

			/* code should be in kbase_tmem_import and its helpers, but uk dropped its get_user abstraction */
			switch (tmem_import->type) {
#ifdef CONFIG_UMP
			case BASE_TMEM_IMPORT_TYPE_UMP:
				get_user(handle, phandle);
				break;
#endif				/* CONFIG_UMP */
			case BASE_TMEM_IMPORT_TYPE_UMM:
				get_user(handle, phandle);
				break;
			default:
				goto bad_type;
				break;
			}
#ifdef CONFIG_TGL_KERNEL
			reg = kbase_tmem_import(kctx, tmem_import->type, handle, &tmem_import->pages, tmem_import->tgl_params);
#else
			reg = kbase_tmem_import(kctx, tmem_import->type, handle, &tmem_import->pages);
#endif /* CONFIG_TGL_KERNEL */

			if (reg) {
				tmem_import->gpu_addr = reg->start_pfn << PAGE_SHIFT;
			} else {
 bad_type:
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}
	case KBASE_FUNC_PMEM_ALLOC:
		{
			kbase_uk_pmem_alloc *pmem = args;
			struct kbase_va_region *reg;

			if (sizeof(*pmem) != args_size)
				goto bad_size;

			reg = kbase_pmem_alloc(kctx, pmem->vsize, pmem->flags, &pmem->cookie);
			if (!reg)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_MEM_FREE:
		{
			kbase_uk_mem_free *mem = args;

			if (sizeof(*mem) != args_size)
				goto bad_size;

			if ((mem->gpu_addr & ~PAGE_MASK) && (mem->gpu_addr >= PAGE_SIZE)) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_MEM_FREE: mem->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_mem_free(kctx, mem->gpu_addr))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_JOB_SUBMIT:
		{
			kbase_uk_job_submit *job = args;

			if (sizeof(*job) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_jd_submit(kctx, job))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_SYNC:
		{
			kbase_uk_sync_now *sn = args;

			if (sizeof(*sn) != args_size)
				goto bad_size;

			if (sn->sset.basep_sset.mem_handle & ~PAGE_MASK) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_SYNC: sn->sset.basep_sset.mem_handle: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (MALI_ERROR_NONE != kbase_sync_now(kctx, &sn->sset))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_POST_TERM:
		{
			kbase_event_close(kctx);
			break;
		}

	case KBASE_FUNC_HWCNT_SETUP:
		{
			kbase_uk_hwcnt_setup *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_instr_hwcnt_setup(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_HWCNT_DUMP:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_dump(kctx))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_HWCNT_CLEAR:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_clear(kctx))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_CPU_PROPS_REG_DUMP:
		{
			kbase_uk_cpuprops *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_cpuprops_uk_get_props(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_GPU_PROPS_REG_DUMP:
		{
			kbase_uk_gpuprops *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_gpuprops_uk_get_props(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_TMEM_GETSIZE:
		{
			kbase_uk_tmem_get_size *getsize = args;
			if (sizeof(*getsize) != args_size)
				goto bad_size;

			if (getsize->gpu_addr & ~PAGE_MASK) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_TMEM_GETSIZE: getsize->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_tmem_get_size(kctx, getsize->gpu_addr, &getsize->actual_size);
			break;
		}
		break;

	case KBASE_FUNC_TMEM_SETSIZE:
		{
			kbase_uk_tmem_set_size *set_size = args;

			if (sizeof(*set_size) != args_size)
				goto bad_size;

			if (set_size->gpu_addr & ~PAGE_MASK) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_TMEM_SETSIZE: set_size->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_tmem_set_size(kctx, set_size->gpu_addr, set_size->size, &set_size->actual_size, (base_backing_threshold_status * const )&set_size->result_subcode);
			break;
		}

	case KBASE_FUNC_TMEM_RESIZE:
		{
			kbase_uk_tmem_resize *resize = args;
			if (sizeof(*resize) != args_size)
				goto bad_size;

			if (resize->gpu_addr & ~PAGE_MASK) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_TMEM_RESIZE: resize->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_tmem_resize(kctx, resize->gpu_addr, resize->delta, &resize->actual_size, (base_backing_threshold_status * const )&resize->result_subcode);
			break;
		}

	case KBASE_FUNC_FIND_CPU_MAPPING:
		{
			kbase_uk_find_cpu_mapping *find = args;
			struct kbase_cpu_mapping *map;

			if (sizeof(*find) != args_size)
				goto bad_size;

			if (find->gpu_addr & ~PAGE_MASK) {
				KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kbase_dispatch case KBASE_FUNC_FIND_CPU_MAPPING: find->gpu_addr: passed parameter is invalid");
				goto out_bad;
			}

			KBASE_DEBUG_ASSERT(find != NULL);
			if (find->size > SIZE_MAX || find->cpu_addr > ULONG_MAX)
				map = NULL;
			else
				map = kbasep_find_enclosing_cpu_mapping(kctx, find->gpu_addr, (void *)(uintptr_t) find->cpu_addr, (size_t) find->size);

			if (NULL != map) {
				find->uaddr = PTR_TO_U64(map->uaddr);
				find->nr_pages = map->nr_pages;
				find->page_off = map->page_off;
			} else {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}
	case KBASE_FUNC_GET_VERSION:
		{
			kbase_uk_get_ddk_version *get_version = (kbase_uk_get_ddk_version *) args;

			if (sizeof(*get_version) != args_size)
				goto bad_size;

			/* version buffer size check is made in compile time assert */
			memcpy(get_version->version_buffer, KERNEL_SIDE_DDK_VERSION_STRING, sizeof(KERNEL_SIDE_DDK_VERSION_STRING));
			get_version->version_string_size = sizeof(KERNEL_SIDE_DDK_VERSION_STRING);
			break;
		}

	case KBASE_FUNC_STREAM_CREATE:
		{
#ifdef CONFIG_SYNC
			kbase_uk_stream_create *screate = (kbase_uk_stream_create *) args;

			if (sizeof(*screate) != args_size)
				goto bad_size;

			if (strnlen(screate->name, sizeof(screate->name)) >= sizeof(screate->name)) {
				/* not NULL terminated */
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_stream_create(screate->name, &screate->fd);
#else
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
#endif
			break;
		}
	case KBASE_FUNC_FENCE_VALIDATE:
		{
#ifdef CONFIG_SYNC
			kbase_uk_fence_validate *fence_validate = (kbase_uk_fence_validate *) args;
			if (sizeof(*fence_validate) != args_size)
				goto bad_size;

			ukh->ret = kbase_fence_validate(fence_validate->fd);
#endif				/* CONFIG_SYNC */
			break;
		}

	case KBASE_FUNC_EXT_BUFFER_LOCK:
		{
#ifdef CONFIG_KDS
			ukh->ret = kbase_external_buffer_lock(kctx, (kbase_uk_ext_buff_kds_data *) args, args_size);
#endif				/* CONFIG_KDS */
			break;
		}

	case KBASE_FUNC_SET_TEST_DATA:
		{
#if MALI_UNIT_TEST
			kbase_uk_set_test_data *set_data = args;

			shared_kernel_test_data = set_data->test_data;
			shared_kernel_test_data.kctx.value = kctx;
			shared_kernel_test_data.mm.value = (void *)current->mm;
			ukh->ret = MALI_ERROR_NONE;
#endif				/* MALI_UNIT_TEST */
			break;
		}

	case KBASE_FUNC_INJECT_ERROR:
		{
#ifdef CONFIG_MALI_ERROR_INJECT
			unsigned long flags;
			kbase_error_params params = ((kbase_uk_error_params *) args)->params;
			/*mutex lock */
			spin_lock_irqsave(&kbdev->osdev.reg_op_lock, flags);
			ukh->ret = job_atom_inject_error(&params);
			spin_unlock_irqrestore(&kbdev->osdev.reg_op_lock, flags);
			/*mutex unlock */
#endif				/* CONFIG_MALI_ERROR_INJECT */
			break;
		}

	case KBASE_FUNC_MODEL_CONTROL:
		{
#ifdef CONFIG_MALI_NO_MALI
			unsigned long flags;
			kbase_model_control_params params = ((kbase_uk_model_control_params *) args)->params;
			/*mutex lock */
			spin_lock_irqsave(&kbdev->osdev.reg_op_lock, flags);
			ukh->ret = midg_model_control(kbdev->osdev.model, &params);
			spin_unlock_irqrestore(&kbdev->osdev.reg_op_lock, flags);
			/*mutex unlock */
#endif				/* CONFIG_MALI_NO_MALI */
			break;
		}

	case KBASE_FUNC_KEEP_GPU_POWERED:
		{
			kbase_uk_keep_gpu_powered *kgp = (kbase_uk_keep_gpu_powered *) args;
			/* A suspend won't happen here, because we're in a syscall from a
			 * userspace thread.
			 *
			 * Nevertheless, we'd get the wrong pm_context_active/idle counting
			 * here if a suspend did happen, so let's assert it won't: */
			KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

			if (kgp->enabled && !kctx->keep_gpu_powered) {
				kbase_pm_context_active(kbdev);
				atomic_inc(&kbdev->keep_gpu_powered_count);
				kctx->keep_gpu_powered = MALI_TRUE;
			} else if (!kgp->enabled && kctx->keep_gpu_powered) {
				atomic_dec(&kbdev->keep_gpu_powered_count);
				kbase_pm_context_idle(kbdev);
				kctx->keep_gpu_powered = MALI_FALSE;
			}

			break;
		}

		case KBASE_FUNC_SET_DVFS_MAXFREQ:
		{
			kbase_uk_set_maxfreq *freqInfo = (kbase_uk_set_maxfreq *)args;
			dvfs_set_max_frequency((freqInfo->dvfs_maxfreq)*1000);
		}
		break;

		case KBASE_FUNC_SET_DVFS_INFO:
		{
			kbase_uk_set_dvfs *levelInfo = (kbase_uk_set_dvfs *)args;
			g_u32DVFS_Manual_Level = levelInfo->dvfs_info;
		}
		break;

		case KBASE_FUNC_GET_GPU_STATUS:
		{
			int status; 
			kbase_uk_get_gpu_status *statusInfo = args;			
			status = atomic_read(&kbdev->gpu_fail);
			copy_to_user(statusInfo->param, &status, sizeof(int));
		}
		break;

	case KBASE_FUNC_MEM_INFO:
		{
				kbase_get_mem_info(kctx);
		}
	default:
		dev_err(kbdev->osdev.dev, "unknown ioctl %u", id);
		goto out_bad;
	}

	return MALI_ERROR_NONE;

 bad_size:
	dev_err(kbdev->osdev.dev, "Wrong syscall size (%d) for %08x\n", args_size, id);
 out_bad:
	return MALI_ERROR_FUNCTION_FAILED;
}

static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

/* Find a particular kbase device (as specified by minor number), or find the "first" device if -1 is specified */
struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
	struct list_head *entry;

	down(&kbase_dev_list_lock);
	list_for_each(entry, &kbase_dev_list) {
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, osdev.entry);
		if (tmp->osdev.mdev.minor == minor || minor == -1) {
			kbdev = tmp;
			get_device(kbdev->osdev.dev);
			break;
		}
	}
	up(&kbase_dev_list_lock);

	return kbdev;
}
EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
	put_device(kbdev->osdev.dev);
}
EXPORT_SYMBOL(kbase_release_device);

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	kbase_context *kctx;
	int ret = 0;

	kbdev = kbase_find_device(iminor(inode));

	if (!kbdev)
		return -ENODEV;

	kctx = kbase_create_context(kbdev);
	if (!kctx) {
		ret = -ENOMEM;
		goto out;
	}

	init_waitqueue_head(&kctx->osctx.event_queue);
	filp->private_data = kctx;

	dev_dbg(kbdev->osdev.dev, "created base context\n");

	{
		kbasep_kctx_list_element *element;

		element = kzalloc(sizeof(kbasep_kctx_list_element), GFP_KERNEL);
		if (element) {
			mutex_lock(&kbdev->kctx_list_lock);
			element->kctx = kctx;
			list_add(&element->link, &kbdev->kctx_list);
			mutex_unlock(&kbdev->kctx_list_lock);
		} else {
			/* we don't treat this as a fail - just warn about it */
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM," %s : couldn't add kctx to kctx_list\n",  KBASE_DRV_NAME);
		}
	}
	return 0;

 out:
	kbase_release_device(kbdev);
	return ret;
}

static int kbase_release(struct inode *inode, struct file *filp)
{
	kbase_context *kctx = filp->private_data;
	struct kbase_device *kbdev = kctx->kbdev;
	kbasep_kctx_list_element *element, *tmp;
	mali_bool found_element = MALI_FALSE;

	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry_safe(element, tmp, &kbdev->kctx_list, link) {
		if (element->kctx == kctx) {
			list_del(&element->link);
			kfree(element);
			found_element = MALI_TRUE;
		}
	}
	mutex_unlock(&kbdev->kctx_list_lock);
	if (!found_element)
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "%s : kctx not in kctx_list\n", KBASE_DRV_NAME);

	filp->private_data = NULL;
	kbase_destroy_context(kctx);

	dev_dbg(kbdev->osdev.dev, "deleted base context\n");
	kbase_release_device(kbdev);
	return 0; 
}

#define CALL_MAX_SIZE 512

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u64 msg[(CALL_MAX_SIZE + 7) >> 3] = { 0xdeadbeefdeadbeefull };	/* alignment fixup */
	u32 size = _IOC_SIZE(cmd);
	kbase_context *kctx = filp->private_data;

	if (size > CALL_MAX_SIZE)
		return -ENOTTY;

	if (0 != copy_from_user(&msg, (void *)arg, size)) {
		pr_err("failed to copy ioctl argument into kernel space\n");
		return -EFAULT;
	}

	if (MALI_ERROR_NONE != kbase_dispatch(kctx, &msg, size))
		return -EFAULT;

	if (0 != copy_to_user((void *)arg, &msg, size)) {
		pr_err("failed to copy results of UK call back to user space\n");
		return -EFAULT;
	}
	return 0;
}

static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	kbase_context *kctx = filp->private_data;
	base_jd_event_v2 uevent;
	int out_count = 0;

	if (count < sizeof(uevent))
		return -ENOBUFS;

	do {
		while (kbase_event_dequeue(kctx, &uevent)) {
			if (out_count > 0)
				goto out;

			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;

			if (wait_event_interruptible(kctx->osctx.event_queue, kbase_event_pending(kctx)))
				return -ERESTARTSYS;
		}
		if (uevent.event_code == BASE_JD_EVENT_DRV_TERMINATED) {
			if (out_count == 0)
				return -EPIPE;
			goto out;
		}

		if (copy_to_user(buf, &uevent, sizeof(uevent)))
			return -EFAULT;

		buf += sizeof(uevent);
		out_count++;
		count -= sizeof(uevent);
	} while (count >= sizeof(uevent));

 out:
	return out_count * sizeof(uevent);
}

static unsigned int kbase_poll(struct file *filp, poll_table *wait)
{
	kbase_context *kctx = filp->private_data;

	poll_wait(filp, &kctx->osctx.event_queue, wait);
	if (kbase_event_pending(kctx))
		return POLLIN | POLLRDNORM;

	return 0;
}

void kbase_event_wakeup(kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	wake_up_interruptible(&kctx->osctx.event_queue);
}

KBASE_EXPORT_TEST_API(kbase_event_wakeup)

int kbase_check_flags(int flags)
{
	/* Enforce that the driver keeps the O_CLOEXEC flag so that execve() always
	 * closes the file descriptor in a child process.
	 */
	if (0 == (flags & O_CLOEXEC))
		return -EINVAL;

	return 0;
}

static const struct file_operations kbase_fops = {
	.owner = THIS_MODULE,
	.open = kbase_open,
	.release = kbase_release,
	.read = kbase_read,
	.poll = kbase_poll,
	.unlocked_ioctl = kbase_ioctl,
	.mmap = kbase_mmap,
	.check_flags = kbase_check_flags,
};

#ifndef CONFIG_MALI_NO_MALI
void kbase_os_reg_write(kbase_device *kbdev, u16 offset, u32 value)
{
	writel(value, kbdev->osdev.reg + offset);
}

u32 kbase_os_reg_read(kbase_device *kbdev, u16 offset)
{
	return readl(kbdev->osdev.reg + offset);
}

static void *kbase_tag(void *ptr, u32 tag)
{
	return (void *)(((uintptr_t) ptr) | tag);
}

static void *kbase_untag(void *ptr)
{
	return (void *)(((uintptr_t) ptr) & ~3);
}

static irqreturn_t kbase_job_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if ( val && kbdev->pm.job_irq_mask == 0)
		dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val );
#endif

	/* Prevent IRQs from reaching the driver if we have not enabled them, this is to prevent
	 * spurious IRQs being processed before the driver is ready to receive them.
	 */

	val &= kbdev->pm.job_irq_mask;

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_job_done(kbdev, val);

	return IRQ_HANDLED;
}

KBASE_EXPORT_TEST_API(kbase_job_irq_handler);

static irqreturn_t kbase_mmu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if ( val && kbdev->pm.mmu_irq_mask == 0)
		dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val );
#endif

	/* Prevent IRQs from reaching the driver if we have not enabled them, this is to prevent
	 * spurious IRQs being processed before the driver is ready to receive them.
	 */

	val &= kbdev->pm.mmu_irq_mask;

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_mmu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_gpu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if ( val && kbdev->pm.gpu_irq_mask == 0)
		dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val );
#endif

	/* Prevent IRQs from reaching the driver if we have not enabled them, this is to prevent
	 * spurious IRQs being processed before the driver is ready to receive them.
	 */

	val &= kbdev->pm.gpu_irq_mask;

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_gpu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irq_handler_t kbase_handler_table[] = {
	[JOB_IRQ_TAG] = kbase_job_irq_handler,
	[MMU_IRQ_TAG] = kbase_mmu_irq_handler,
	[GPU_IRQ_TAG] = kbase_gpu_irq_handler,
};

#ifdef CONFIG_MALI_DEBUG
#define  JOB_IRQ_HANDLER JOB_IRQ_TAG
#define  MMU_IRQ_HANDLER MMU_IRQ_TAG
#define  GPU_IRQ_HANDLER GPU_IRQ_TAG

/**
 * @brief Registers given interrupt handler for requested interrupt type
 *        Case irq handler is not specified default handler shall be registered
 *
 * @param[in] kbdev           - Device for which the handler is to be registered
 * @param[in] custom_handler  - Handler to be registered
 * @param[in] irq_type        - Interrupt type
 * @return	MALI_ERROR_NONE case success, MALI_ERROR_FUNCTION_FAILED otherwise
 */
static mali_error kbase_set_custom_irq_handler(kbase_device *kbdev, irq_handler_t custom_handler, int irq_type)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	mali_error result = MALI_ERROR_NONE;
	irq_handler_t requested_irq_handler = NULL;
	KBASE_DEBUG_ASSERT((JOB_IRQ_HANDLER <= irq_type) && (GPU_IRQ_HANDLER >= irq_type));

	/* Release previous handler */
	if (osdev->irqs[irq_type].irq)
		free_irq(osdev->irqs[irq_type].irq, kbase_tag(kbdev, irq_type));

	requested_irq_handler = (NULL != custom_handler) ? custom_handler : kbase_handler_table[irq_type];

	if (0 != request_irq(osdev->irqs[irq_type].irq, requested_irq_handler, osdev->irqs[irq_type].flags | IRQF_SHARED, dev_name(osdev->dev), kbase_tag(kbdev, irq_type))) {
		result = MALI_ERROR_FUNCTION_FAILED;
		dev_err(osdev->dev, "Can't request interrupt %d (index %d)\n", osdev->irqs[irq_type].irq, irq_type);
#ifdef CONFIG_SPARSE_IRQ
		dev_err(osdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif				/* CONFIG_SPARSE_IRQ */
	}

	return result;
}

KBASE_EXPORT_TEST_API(kbase_set_custom_irq_handler)

/* test correct interrupt assigment and reception by cpu */
typedef struct kbasep_irq_test {
	struct hrtimer timer;
	wait_queue_head_t wait;
	int triggered;
	u32 timeout;
} kbasep_irq_test;

static kbasep_irq_test kbasep_irq_test_data;

#define IRQ_TEST_TIMEOUT    500

static irqreturn_t kbase_job_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_mmu_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static enum hrtimer_restart kbasep_test_interrupt_timeout(struct hrtimer *timer)
{
	kbasep_irq_test *test_data = container_of(timer, kbasep_irq_test, timer);

	test_data->timeout = 1;
	test_data->triggered = 1;
	wake_up(&test_data->wait);
	return HRTIMER_NORESTART;
}

static mali_error kbasep_common_test_interrupt(kbase_device * const kbdev, u32 tag)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	mali_error err = MALI_ERROR_NONE;
	irq_handler_t test_handler;

	u32 old_mask_val;
	u16 mask_offset;
	u16 rawstat_offset;

	switch (tag) {
	case JOB_IRQ_TAG:
		test_handler = kbase_job_irq_test_handler;
		rawstat_offset = JOB_CONTROL_REG(JOB_IRQ_RAWSTAT);
		mask_offset = JOB_CONTROL_REG(JOB_IRQ_MASK);
		break;
	case MMU_IRQ_TAG:
		test_handler = kbase_mmu_irq_test_handler;
		rawstat_offset = MMU_REG(MMU_IRQ_RAWSTAT);
		mask_offset = MMU_REG(MMU_IRQ_MASK);
		break;
	case GPU_IRQ_TAG:
		/* already tested by pm_driver - bail out */
	default:
		return MALI_ERROR_NONE;
	}

	/* store old mask */
	old_mask_val = kbase_reg_read(kbdev, mask_offset, NULL);
	/* mask interrupts */
	kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

	if (osdev->irqs[tag].irq) {
		/* release original handler and install test handler */
		if (MALI_ERROR_NONE != kbase_set_custom_irq_handler(kbdev, test_handler, tag)) {
			err = MALI_ERROR_FUNCTION_FAILED;
		} else {
			kbasep_irq_test_data.timeout = 0;
			hrtimer_init(&kbasep_irq_test_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			kbasep_irq_test_data.timer.function = kbasep_test_interrupt_timeout;

			/* trigger interrupt */
			kbase_reg_write(kbdev, mask_offset, 0x1, NULL);
			kbase_reg_write(kbdev, rawstat_offset, 0x1, NULL);

			hrtimer_start(&kbasep_irq_test_data.timer, HR_TIMER_DELAY_MSEC(IRQ_TEST_TIMEOUT), HRTIMER_MODE_REL);

			wait_event(kbasep_irq_test_data.wait, kbasep_irq_test_data.triggered != 0);

			if (kbasep_irq_test_data.timeout != 0) {
				dev_err(osdev->dev, "Interrupt %d (index %d) didn't reach CPU.\n", osdev->irqs[tag].irq, tag);
				err = MALI_ERROR_FUNCTION_FAILED;
			} else {
				dev_dbg(osdev->dev, "Interrupt %d (index %d) reached CPU.\n", osdev->irqs[tag].irq, tag);
			}

			hrtimer_cancel(&kbasep_irq_test_data.timer);
			kbasep_irq_test_data.triggered = 0;

			/* mask interrupts */
			kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

			/* release test handler */
			free_irq(osdev->irqs[tag].irq, kbase_tag(kbdev, tag));
		}

		/* restore original interrupt */
		if (request_irq(osdev->irqs[tag].irq, kbase_handler_table[tag], osdev->irqs[tag].flags | IRQF_SHARED, dev_name(osdev->dev), kbase_tag(kbdev, tag))) {
			dev_err(osdev->dev, "Can't restore original interrupt %d (index %d)\n", osdev->irqs[tag].irq, tag);
			err = MALI_ERROR_FUNCTION_FAILED;
		}
	}
	/* restore old mask */
	kbase_reg_write(kbdev, mask_offset, old_mask_val, NULL);

	return err;
}

static mali_error kbasep_common_test_interrupt_handlers(kbase_device * const kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	mali_error err;

	init_waitqueue_head(&kbasep_irq_test_data.wait);
	kbasep_irq_test_data.triggered = 0;

	/* A suspend won't happen during startup/insmod */
	kbase_pm_context_active(kbdev);

	err = kbasep_common_test_interrupt(kbdev, JOB_IRQ_TAG);
	if (MALI_ERROR_NONE != err) {
		dev_err(osdev->dev, "Interrupt JOB_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	err = kbasep_common_test_interrupt(kbdev, MMU_IRQ_TAG);
	if (MALI_ERROR_NONE != err) {
		dev_err(osdev->dev, "Interrupt MMU_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	dev_err(osdev->dev, "Interrupts are correctly assigned.\n");

 out:
	kbase_pm_context_idle(kbdev);

	return err;

}
#endif				/* CONFIG_MALI_DEBUG */

static int kbase_install_interrupts(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	int err;
	u32 i;

	BUG_ON(nr > PLATFORM_CONFIG_IRQ_RES_COUNT);	/* Only 3 interrupts! */

	for (i = 0; i < nr; i++) {
		err = request_irq(osdev->irqs[i].irq, kbase_handler_table[i], osdev->irqs[i].flags | IRQF_SHARED, dev_name(osdev->dev), kbase_tag(kbdev, i));
		if (err) {
			dev_err(osdev->dev, "Can't request interrupt %d (index %d)\n", osdev->irqs[i].irq, i);
#ifdef CONFIG_SPARSE_IRQ
			dev_err(osdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif				/* CONFIG_SPARSE_IRQ */
			goto release;
		}
	}

	return 0;

 release:
	while (i-- > 0)
		free_irq(osdev->irqs[i].irq, kbase_tag(kbdev, i));

	return err;
}

static void kbase_release_interrupts(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (osdev->irqs[i].irq)
			free_irq(osdev->irqs[i].irq, kbase_tag(kbdev, i));
	}
}

void kbase_synchronize_irqs(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (osdev->irqs[i].irq)
			synchronize_irq(osdev->irqs[i].irq);
	}
}

#endif				/* CONFIG_MALI_NO_MALI */

/** Show callback for the @c gpu_memory sysfs file.
 *
 * This function is called to get the contents of the @c gpu_memory sysfs
 * file. This is a report of current gpu memory usage.
 *
 * @param dev  The device this sysfs file is for
 * @param attr The attributes of the sysfs file
 * @param buf  The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_gpu_memory(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct list_head *entry;

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "Name              cap(pages) usage(pages)\n" "=========================================\n");
	down(&kbase_dev_list_lock);
	list_for_each(entry, &kbase_dev_list) {
		struct kbase_device *kbdev = NULL;
		kbasep_kctx_list_element *element;

		kbdev = list_entry(entry, struct kbase_device, osdev.entry);
		/* output the total memory usage and cap for this device */
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%-16s  %10u   %10u\n", kbdev->osdev.devname, kbdev->memdev.usage.max_pages, atomic_read(&(kbdev->memdev.usage.cur_pages))
		    );
		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(element, &kbdev->kctx_list, link) {
			/* output the memory usage and cap for each kctx opened on this device */
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "  %s-0x%p %10u   %10u\n", "kctx", element->kctx, element->kctx->usage.max_pages, atomic_read(&(element->kctx->usage.cur_pages))
			    );
		}
		mutex_unlock(&kbdev->kctx_list_lock);
	}
	up(&kbase_dev_list_lock);
	if (PAGE_SIZE == ret) {
		/* we attempted to write more than a page full - truncate */
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}
	return ret;
}

/** The sysfs file @c gpu_memory.
 *
 * This is used for obtaining a report of current gpu memory usage.
 */
DEVICE_ATTR(gpu_memory, S_IRUGO, show_gpu_memory, NULL);


/** Show callback for the @c power_policy sysfs file.
 *
 * This function is called to get the contents of the @c power_policy sysfs
 * file. This is a list of the available policies with the currently active one
 * surrounded by square brackets.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_policy(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *current_policy;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	current_policy = kbase_pm_get_policy(kbdev);

	policy_count = kbase_pm_list_policies(&policy_list);

	for (i = 0; i < policy_count && ret < PAGE_SIZE; i++) {
		if (policy_list[i] == current_policy)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "[%s] ", policy_list[i]->name);
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s ", policy_list[i]->name);
	}

	if (ret < PAGE_SIZE - 1) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/** Store callback for the @c power_policy sysfs file.
 *
 * This function is called when the @c power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls @ref kbase_pm_set_policy to change the
 * policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_policy(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *new_policy = NULL;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	policy_count = kbase_pm_list_policies(&policy_list);

	for (i = 0; i < policy_count; i++) {
		if (sysfs_streq(policy_list[i]->name, buf)) {
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy) {
		dev_err(dev, "power_policy: policy not found\n");
		return -EINVAL;
	}

	kbase_pm_set_policy(kbdev, new_policy);
	return count;
}

/** The sysfs file @c power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
DEVICE_ATTR(power_policy, S_IRUGO | S_IWUSR, show_policy, set_policy);
/* dvfs on/off */
static ssize_t show_dvfs_on(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	ret = sprintf(buf, "%d\n", g_DVFS_OnOFF);
	
	return ret;
}

/** Store callback for the @c power_policy sysfs file.
 *
 * This function is called when the @c power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls @ref kbase_pm_set_policy to change the
 * policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_dvfs_on(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int on;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "dvfs on = %u\n", on);

	g_bThermal_limit= MALI_TRUE;

	if (on == 1)
	{
		if (g_DVFS_OnOFF == DVFS_ON)
			goto out_dvfs_on;

		g_DVFS_OnOFF = DVFS_ON;
		dev_info(dev, "dvfs ON\n");
		
	}
	else if (on == 0)
	{
		if (g_DVFS_OnOFF == DVFS_OFF)
			goto out_dvfs_on;

		g_DVFS_OnOFF = DVFS_OFF;
		dev_info(dev, "dvfs OFF\n");
		
		g_u32DVFS_Manual_Level = MAX_GPU_FREQ_LEVEL;
	}

	else if (on == 2)
	{
		if (g_DVFS_OnOFF == DVFS_OFF)
			goto out_dvfs_on;

		g_DVFS_OnOFF = DVFS_FIX;		
		g_bThermal_limit= MALI_FALSE;
		dev_info(dev, "dvfs FIX_value\n");		
	}
		
	else
	{
		dev_err(dev, "invalid arg %u. input 0 or 1, 2\n", on);
	}

out_dvfs_on:
	return count;
}

/** The sysfs file @c power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
DEVICE_ATTR(dvfs_on, S_IRUGO|S_IWUSR, show_dvfs_on, set_dvfs_on);


/* dvfs on/off */
static ssize_t show_dvfs_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
	/* W0000135754 */
	if(g_Thermal_limit_50 == MALI_TRUE)
	{
		ret = sprintf(buf, "%u\n", 50);
	}
	else
	{
		ret = sprintf(buf, "%u\n", GolfP_freq_table[g_u32DVFS_CurLevel].frequency);
	}	

	return ret;
}
/** Store callback for the @c power_policy sysfs file.
 *
 * This function is called when the @c power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls @ref kbase_pm_set_policy to change the
 * policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_dvfs_level(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int gpu_freq=0, level=0, setlevel=0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
		
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, "This sysfs can be used when DVFS_OFF is set. \n");
		goto out_dvfs_level;
	}

	ret = sscanf(buf, "%u", &gpu_freq);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if( ((unsigned int)gpu_freq > GolfP_freq_table[min_support_idx].frequency) || ((unsigned int)gpu_freq < GolfP_freq_table[max_real_idx].frequency) )
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " Not support Frequncy level [%u] KHz  MAX value [%u] KHz MIN Value[%u]\n", gpu_freq, GolfP_freq_table[min_support_idx].frequency, GolfP_freq_table[max_real_idx].frequency);
		goto out_dvfs_level;
	}

	for(level= min_support_idx; level <= (max_real_idx+1); level++)
	{
		if(GolfP_freq_table[level-1].frequency == (unsigned int)gpu_freq)
		{
			setlevel = level-1;
			dev_info(dev,"gpu freq = %u KHz, set Freq = %u KHz\n", gpu_freq, GolfP_freq_table[level-1].frequency);
			break;
		}
                            
	 	else if( ((unsigned int)gpu_freq < GolfP_freq_table[level-1].frequency) && (GolfP_freq_table[level].frequency < (unsigned int)gpu_freq))
		{
			setlevel = level-1;
			dev_info(dev,"gpu freq = %u KHz, set Freq = %u KHz\n", gpu_freq, GolfP_freq_table[level-1].frequency);
		}
	}

	if(((unsigned int)gpu_freq) <= GolfP_freq_table[max_support_idx].frequency)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " gpu freq = %u KHz, set Freq = %u KHz\n", gpu_freq, GolfP_freq_table[max_support_idx].frequency);
		setlevel = max_support_idx;
	}

	g_u32DVFS_Manual_Level = GolfP_freq_table[setlevel].index;
	g_u32DVFS_FixLevel = GolfP_freq_table[setlevel].index;

out_dvfs_level:
	return count;

}

/** The sysfs file @c power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
DEVICE_ATTR(dvfs_level, S_IRUGO|S_IWUSR, show_dvfs_level, set_dvfs_level);

static ssize_t show_max_limit(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	dev_info(dev,"gpu Max Freq = %u KHz\n", GolfP_freq_table[min_support_idx].frequency);

	return ret;
}

static ssize_t set_max_limit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int gpu_freq;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
		
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		KBASE_DEBUG_PRINT (KBASE_MEM, "This sysfs can be used when DVFS_OFF is set. \n");
		goto out_dvfs_level;
	}

	ret = sscanf(buf, "%u", &gpu_freq);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dvfs_set_max_frequency(gpu_freq);

out_dvfs_level:
	return count;

}


DEVICE_ATTR(dvfs_maxfreq_limit, S_IRUGO|S_IWUSR, show_max_limit, set_max_limit);

static ssize_t show_min_lock(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	dev_info(dev,"gpu Max Freq = %u KHz\n", GolfP_freq_table[max_real_idx].frequency);

	return ret;
}


static ssize_t set_min_lock(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int gpu_freq;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
		
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, "This sysfs can be used when DVFS_OFF is set. \n");
		goto out_dvfs_level;
	}

	ret = sscanf(buf, "%u", &gpu_freq);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dvfs_set_min_frequency(gpu_freq);

out_dvfs_level:
	return count;

}


DEVICE_ATTR(dvfs_minfreq_lock, S_IRUGO|S_IWUSR, show_min_lock, set_min_lock);


static ssize_t show_cpulock_threshold(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	dev_info(dev,"cpulock_threshold= %u KHz --> %u KHz\n", (GolfP_freq_table[cpu_lock_threshold].frequency)/1000, (GolfP_freq_table[cpu_lock_threshold-1].frequency)/1000);

	return ret;
}

static ssize_t set_cpulock_threshold(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int gpu_freq=0, level=0, setlevel=0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
		
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, "This sysfs can be used when DVFS_OFF is set. \n");
		goto out_cpu_threshold;
	}

	ret = sscanf(buf, "%u", &gpu_freq);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if( ((unsigned int)gpu_freq > GolfP_freq_table[min_support_idx].frequency) || ((unsigned int)gpu_freq < GolfP_freq_table[max_real_idx].frequency) )
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " Not support Frequncy level [%u] KHz  MAX value [%u] KHz MIN Value[%u]\n", gpu_freq, GolfP_freq_table[min_support_idx].frequency, GolfP_freq_table[max_real_idx].frequency);
		goto out_cpu_threshold;
	}

	for(level= min_support_idx; level <= (max_real_idx+1); level++)
	{
		if(GolfP_freq_table[level-1].frequency == (unsigned int)gpu_freq)
		{
			setlevel = level-1;
			dev_info(dev,"cpulock_threshold= %u KHz --> %u KHz\n", (GolfP_freq_table[setlevel].frequency)/1000, (GolfP_freq_table[setlevel-1].frequency)/1000);
			break;
		}
                            
	 	else if( ((unsigned int)gpu_freq < GolfP_freq_table[level-1].frequency) && (GolfP_freq_table[level].frequency < (unsigned int)gpu_freq))
		{
			setlevel = level-1;
		}
	}

	if(((unsigned int)gpu_freq) <= GolfP_freq_table[max_support_idx].frequency)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " gpu freq = %u KHz, set Freq = %u KHz\n", gpu_freq, GolfP_freq_table[max_support_idx].frequency);
		setlevel = max_support_idx;
	}

	
	cpu_lock_threshold = GolfP_freq_table[setlevel].index;

out_cpu_threshold:
	return count;

}
DEVICE_ATTR(cpu_lock_value, S_IRUGO|S_IWUSR, show_cpulock_threshold, set_cpulock_threshold);

static ssize_t show_cpu_unlock_threshold(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	dev_info(dev,"cpulock_threshold= %u KHz --> %u KHz\n", (GolfP_freq_table[cpu_lock_threshold].frequency)/1000, (GolfP_freq_table[cpu_lock_threshold-1].frequency)/1000);

	return ret;
}

static ssize_t set_cpu_unlock_threshold(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int gpu_freq=0, level=0, setlevel=0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
		
	if(g_DVFS_OnOFF == DVFS_ON)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, "This sysfs can be used when DVFS_OFF is set. \n");
		goto out_cpu_threshold;
	}

	ret = sscanf(buf, "%u", &gpu_freq);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	if( ((unsigned int)gpu_freq > GolfP_freq_table[min_support_idx].frequency) || ((unsigned int)gpu_freq < GolfP_freq_table[max_real_idx].frequency) )
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " Not support Frequncy level [%u] KHz  MAX value [%u] KHz MIN Value[%u]\n", gpu_freq, GolfP_freq_table[min_support_idx].frequency, GolfP_freq_table[max_real_idx].frequency);
		goto out_cpu_threshold;
	}

	for(level= min_support_idx; level <= (max_real_idx+1); level++)
	{
		if(GolfP_freq_table[level-1].frequency == (unsigned int)gpu_freq)
		{
			setlevel = level-1;
			dev_info(dev,"cpu_unlock_threshold= %u KHz below\n", (GolfP_freq_table[setlevel].frequency)/1000);
			break;
		}
                            
	 	else if( ((unsigned int)gpu_freq < GolfP_freq_table[level-1].frequency) && (GolfP_freq_table[level].frequency < (unsigned int)gpu_freq))
		{
			setlevel = level-1;
		}
	}

	if(((unsigned int)gpu_freq) <= GolfP_freq_table[max_support_idx].frequency)
	{
		KBASE_DEBUG_PRINT(KBASE_MEM, " gpu freq = %u KHz, set Freq = %u KHz\n", gpu_freq, GolfP_freq_table[max_support_idx].frequency);
		setlevel = max_support_idx;
	}

	
	cpu_unlock_threshold = GolfP_freq_table[setlevel].index;

out_cpu_threshold:
	return count;

}
DEVICE_ATTR(cpu_unlock_value, S_IRUGO|S_IWUSR, show_cpu_unlock_threshold, set_cpu_unlock_threshold);

/* asv on/off */
static ssize_t show_ASV_on(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	ret = sprintf(buf, "%d\n", g_bASV_OnOFF);
	
	return ret;
}

/** Store callback for the @c power_policy sysfs file.
 *
 * This function is called when the @c power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls @ref kbase_pm_set_policy to change the
 * policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_ASV_on(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int on;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "ASV on = %u\n", on); /* W0000119450 */

	if (on == 1)
	{
		if (g_bASV_OnOFF == MALI_TRUE)
			goto out_asv_on;

		g_bASV_OnOFF = MALI_TRUE;
		dev_info(dev, "ASV ON\n"); /* W0000119450 */
		
	}
	else if (on == 0)
	{
		if (g_bASV_OnOFF == MALI_FALSE)
			goto out_asv_on;

		g_bASV_OnOFF = MALI_TRUE; /* W0000116302 : [GPU] AVS always on */
		dev_info(dev, "ASV OFF\n"); /* W0000119450 */
	}
	else
	{
		dev_err(dev, "invalid arg %u. input 0 or 1\n", on);
	}

out_asv_on:
	return count;
}

/** The sysfs file @c power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
DEVICE_ATTR(avs_on, S_IRUGO|S_IWUSR, show_ASV_on, set_ASV_on); /* W0000119450 */

static ssize_t show_voltage_info(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i;

	for (i = min_support_idx; i <= max_real_idx ; i++) 
		KBASE_DEBUG_PRINT(KBASE_MEM, "ASV[%d], [%d] %duV\n", gpu_result_of_asv, i, GolfP_Volt_table[i]);		

	return ret;
}

static ssize_t set_voltage_info(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i, j;
	unsigned int volt_table[MIN_GPU_FREQ_LEVEL][GPUFREQ_ASV_COUNT] = {{0,},};
	size_t size, read_cnt = 0;
	char atoi_buf[15];
	char temp;
	int line_cnt = 0, char_cnt;
	bool started = 0, loop = 1;

	/* store to memory */
	memset(volt_table, 0, sizeof(volt_table));
	
	size = count;

	i = 0;
	j = 0;
	char_cnt = 0;
	while (size > read_cnt && loop) {
		/* get 1 byte */
		temp = buf[read_cnt++];
		
		/* find 'S' */
		if (started == 0 && temp == 'S') {
			/* find '\n' */
			while (size > read_cnt) {
				temp = buf[read_cnt++];
				if (temp == '\n') {
					started = 1;
					break;
				}
			}
			continue;
		}

		if (started == 0)
			continue;

		/* check volt table line count */
		if (i > MIN_GPU_FREQ_LEVEL) {
			KBASE_DEBUG_PRINT_ERROR(KBASE_MEM, "gpufreq ERR: volt table line count is more than %d, i = %d\n", MIN_GPU_FREQ_LEVEL, i);
			goto out;
		}

		/* check volt table column count */
		if (j > GPUFREQ_ASV_COUNT) {
			KBASE_DEBUG_PRINT_ERROR(KBASE_MEM, "gpufreq ERR: volt table column count is more than %d, j = %d\n", GPUFREQ_ASV_COUNT, j);
			goto out;
		}

		/* parsing */
		switch (temp) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				atoi_buf[char_cnt] = temp;
				char_cnt++;
				break;

			case ',':
				atoi_buf[char_cnt++] = 0;
				volt_table[i][j] = (unsigned int)simple_strtoul(atoi_buf, (char **)&atoi_buf, 0);
				j++;
				char_cnt = 0;
				break;

			case '\n':
				i++;
				j = 0;
				break;
			
			case 'E':
				loop = 0;
				line_cnt = i;
				break;

			default:
				break;
		}
	}

	/* check line count */
	if (line_cnt != MIN_GPU_FREQ_LEVEL) {
		KBASE_DEBUG_PRINT_ERROR(KBASE_MEM, "gpufreq ERR: volt table line count is not %d\n",MIN_GPU_FREQ_LEVEL);
	
		goto out;
	}

	/* change current volt table */
	KBASE_DEBUG_PRINT(KBASE_MEM, "> DVFS volt table change\n");

	for (i = 0, j = 0; i < MIN_GPU_FREQ_LEVEL; i++, j++) {
		KBASE_DEBUG_PRINT_ERROR(KBASE_MEM, "ASV[%d], [%d] %uuV -> %uuV\n", gpu_result_of_asv, i, GolfP_Volt_table[i], volt_table[j][gpu_result_of_asv]);
		GolfP_Volt_table[i] = volt_table[j][gpu_result_of_asv];

		if(i == MIN_GPU_FREQ_LEVEL-1)
			GolfP_Volt_table[i+1] = volt_table[j][gpu_result_of_asv];
	}

	KBASE_DEBUG_PRINT(KBASE_MEM, "> DONE\n");
	
out:

	return (ssize_t)count;
}

DEVICE_ATTR(voltage_change, S_IRUGO|S_IWUSR, show_voltage_info, set_voltage_info);

/* Thermal Throttle on/off */
static ssize_t show_thermal_on(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!dev)
	{
		return -ENODEV;
	}

	ret = sprintf(buf, "%d\n", g_bThermal_limit);
	
	return ret;
}

static ssize_t set_thermal_on(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int on;

	if (!dev)
	{
		return -ENODEV;
	}

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "dvfs thermal throttle on = %u\n", on);

	if (on == 1)
	{
		if (g_bThermal_limit == MALI_TRUE)
			goto out;

		g_bThermal_limit = MALI_TRUE;
		dev_info(dev, "DVFS Thermal_limit ON\n"); 
		
	}
	else if (on == 0)
	{
		if (g_bThermal_limit == MALI_FALSE)
			goto out;

		g_bThermal_limit = MALI_FALSE; 
		dev_info(dev, "DVFS Thermal_limit OFF\n");
	}
	else
	{
		dev_err(dev, "invalid arg %u. input 0 or 1\n", on);
	}

out:
	return count;
}

DEVICE_ATTR(thermal_throttle_on, S_IRUGO|S_IWUSR, show_thermal_on, set_thermal_on);


/* dvfs debug print on/off */
static ssize_t show_dbgprint_on(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (!dev)
	{
		return -ENODEV;
	}

	ret = sprintf(buf, "%d\n", g_bDVFS_Print);
	
	return ret;
}

static ssize_t set_dbgprint_on(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int on;

	if (!dev)
	{
		return -ENODEV;
	}

	ret = sscanf(buf, "%u", &on);
	if (ret != 1) 
	{
		dev_err(dev, "%s invalid arg\n", __func__);
		return -EINVAL;
	}

	dev_info(dev, "DVFS dbg print on = %u\n", on);

	if (on == 2)
	{
		if (g_bDVFS_Print == 2)
			goto out;

		g_bDVFS_Print = 2;
		dev_info(dev, "DVFS PRINT ON\n"); 
		
	}

	else if (on == 1)
	{
		if (g_bDVFS_Print == 1)
			goto out;

		g_bDVFS_Print = 1;
		dev_info(dev, "DVFS DBG ON\n"); 
		
	}
	else if (on == 0)
	{
		if (g_bDVFS_Print == MALI_FALSE)
			goto out;

		g_bDVFS_Print = MALI_FALSE; 
		dev_info(dev, "DVFS DBG PRINT OFF\n");
	}
	else
	{
		dev_err(dev, "invalid arg %u. input 0 or 1\n", on);
	}

out:
	return count;
}
DEVICE_ATTR(dvfs_print, S_IRUGO|S_IWUSR, show_dbgprint_on, set_dbgprint_on);


#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
/* Import the external affinity mask variables */
extern u64 mali_js0_affinity_mask;
extern u64 mali_js1_affinity_mask;
extern u64 mali_js2_affinity_mask;

/**
 * Structure containing a single shader affinity split configuration.
 */
typedef struct {
	char const * tag;
	char const * human_readable;
	u64          js0_mask;
	u64          js1_mask;
	u64          js2_mask;
} sc_split_config;

/**
 * Array of available shader affinity split configurations.
 */
static sc_split_config const sc_split_configs[] = 
{
	/* All must be the first config (default). */
	{
		"all", "All cores",
		0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
	},
	{
		"mp1", "MP1 shader core",
		0x1, 0x1, 0x1
	},
	{
		"mp2", "MP2 shader core",
		0x3, 0x3, 0x3
	},
	{
		"mp4", "MP4 shader core",
		0xF, 0xF, 0xF
	},
	{
		"mp1_vf", "MP1 vertex + MP1 fragment shader core",
		0x2, 0x1, 0xFFFFFFFFFFFFFFFFULL
	},
	{
		"mp2_vf", "MP2 vertex + MP2 fragment shader core",
		0xA, 0x5, 0xFFFFFFFFFFFFFFFFULL
	},
	/* This must be the last config. */
	{
		NULL, NULL,
		0x0, 0x0, 0x0
	},
};

/* Pointer to the currently active shader split configuration. */
static sc_split_config const * current_sc_split_config = &sc_split_configs[0];

/** Show callback for the @c sc_split sysfs file
 *
 * Returns the current shader core affinity policy.
 */
static ssize_t show_split(struct device *dev, struct device_attribute *attr, char * const buf)
{
	ssize_t ret;
	/* We know we are given a buffer which is PAGE_SIZE long. Our strings are all guaranteed
	 * to be shorter than that at this time so no length check needed. */
	ret = scnprintf(buf, PAGE_SIZE, "Current sc_split: '%s'\n", current_sc_split_config->tag );
	return ret;
}

/** Store callback for the @c sc_split sysfs file.
 *
 * This function is called when the @c sc_split sysfs file is written to
 * It modifies the system shader core affinity configuration to allow
 * system profiling with different hardware configurations.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_split(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sc_split_config const * config = &sc_split_configs[0];

	/* Try to match: loop until we hit the last "NULL" entry */
	while( config->tag )
	{
		if (sysfs_streq(config->tag, buf))
		{
			current_sc_split_config = config;
			mali_js0_affinity_mask  = config->js0_mask;
			mali_js1_affinity_mask  = config->js1_mask;
			mali_js2_affinity_mask  = config->js2_mask;
			dev_info(dev, "Setting sc_split: '%s'\n", config->tag);
			return count;
		}
		config++;
	}

	/* No match found in config list */
	dev_err(dev, "sc_split: invalid value\n");
	dev_err(dev, "  Possible settings: mp[1|2|4], mp[1|2]_vf\n");
	return -ENOENT;
}

/** The sysfs file @c sc_split
 *
 * This is used for configuring/querying the current shader core work affinity
 * configuration.
 */
DEVICE_ATTR(sc_split, S_IRUGO|S_IWUSR, show_split, set_split);
#endif


#if MALI_CUSTOMER_RELEASE == 0
/** Store callback for the @c js_timeouts sysfs file.
 *
 * This function is called to get the contents of the @c js_timeouts sysfs
 * file. This file contains five values separated by whitespace. The values
 * are basically the same as KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS, BASE_CONFIG_ATTR_JS_RESET_TICKS_NSS
 * configuration values (in that order), with the difference that the js_timeout
 * valus are expressed in MILLISECONDS.
 *
 * The js_timeouts sysfile file allows the current values in
 * use by the job scheduler to get override. Note that a value needs to
 * be other than 0 for it to override the current job scheduler value.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_js_timeouts(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	unsigned long js_soft_stop_ms;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%lu %lu %lu %lu %lu", &js_soft_stop_ms, &js_hard_stop_ms_ss, &js_hard_stop_ms_nss, &js_reset_ms_ss, &js_reset_ms_nss);
	if (items == 5) {
		u64 ticks;

		ticks = js_soft_stop_ms * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_soft_stop_ticks = ticks;

		ticks = js_hard_stop_ms_ss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_ss = ticks;

		ticks = js_hard_stop_ms_nss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_nss = ticks;

		ticks = js_reset_ms_ss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_ss = ticks;

		ticks = js_reset_ms_nss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_nss = ticks;

		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_soft_stop_ticks, js_soft_stop_ms);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_ss, js_hard_stop_ms_ss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_nss, js_hard_stop_ms_nss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_ss, js_reset_ms_ss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_nss, js_reset_ms_nss);

		return count;
	} else {
		dev_err(kbdev->osdev.dev, "Couldn't process js_timeouts write operation.\nUse format " "<soft_stop_ms> <hard_stop_ms_ss> <hard_stop_ms_nss> <reset_ms_ss> <reset_ms_nss>\n");
		return -EINVAL;
	}
}

/** Show callback for the @c js_timeouts sysfs file.
 *
 * This function is called to get the contents of the @c js_timeouts sysfs
 * file. It returns the last set values written to the js_timeouts sysfs file.
 * If the file didn't get written yet, the values will be 0.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_js_timeouts(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 ms;
	unsigned long js_soft_stop_ms;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ms = (u64) kbdev->js_soft_stop_ticks * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_soft_stop_ms = (unsigned long)ms;

	ms = (u64) kbdev->js_hard_stop_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_hard_stop_ms_ss = (unsigned long)ms;

	ms = (u64) kbdev->js_hard_stop_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_hard_stop_ms_nss = (unsigned long)ms;

	ms = (u64) kbdev->js_reset_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_reset_ms_ss = (unsigned long)ms;

	ms = (u64) kbdev->js_reset_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_reset_ms_nss = (unsigned long)ms;

	ret = scnprintf(buf, PAGE_SIZE, "%lu %lu %lu %lu %lu\n", js_soft_stop_ms, js_hard_stop_ms_ss, js_hard_stop_ms_nss, js_reset_ms_ss, js_reset_ms_nss);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/** The sysfs file @c js_timeouts.
 *
 * This is used to override the current job scheduler values for
 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS.
 */
DEVICE_ATTR(js_timeouts, S_IRUGO | S_IWUSR, show_js_timeouts, set_js_timeouts);
#endif				/* MALI_CUSTOMER_RELEASE == 0 */

#ifdef CONFIG_MALI_DEBUG
static ssize_t set_js_softstop_always(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	int softstop_always;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%d", &softstop_always);
	if ((items == 1) && ((softstop_always == 0) || (softstop_always == 1))) {
		kbdev->js_data.softstop_always = (mali_bool) softstop_always;

		dev_info(kbdev->osdev.dev, "Support for softstop on a single context: %s\n", (kbdev->js_data.softstop_always == MALI_FALSE) ? "Disabled" : "Enabled");
		return count;
	} else {
		dev_err(kbdev->osdev.dev, "Couldn't process js_softstop_always write operation.\nUse format " "<soft_stop_always>\n");
		return -EINVAL;
	}
}

static ssize_t show_js_softstop_always(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->js_data.softstop_always);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/**
 * By default, soft-stops are disabled when only a single context is present. The ability to
 * enable soft-stop when only a single context is present can be used for debug and unit-testing purposes.
 * (see CL t6xx_stress_1 unit-test as an example whereby this feature is used.)
 */
DEVICE_ATTR(js_softstop_always, S_IRUGO | S_IWUSR, show_js_softstop_always, set_js_softstop_always);
#endif				/* CONFIG_MALI_DEBUG */

#ifdef CONFIG_MALI_DEBUG
typedef void (kbasep_debug_command_func) (kbase_device *);

typedef enum {
	KBASEP_DEBUG_COMMAND_DUMPTRACE,

	/* This must be the last enum */
	KBASEP_DEBUG_COMMAND_COUNT
} kbasep_debug_command_code;

typedef struct kbasep_debug_command {
	char *str;
	kbasep_debug_command_func *func;
} kbasep_debug_command;

/** Debug commands supported by the driver */
static const kbasep_debug_command debug_commands[] = {
	{
	 .str = "dumptrace",
	 .func = &kbasep_trace_dump,
	 }
};

/** Show callback for the @c debug_command sysfs file.
 *
 * This function is called to get the contents of the @c debug_command sysfs
 * file. This is a list of the available debug commands, separated by newlines.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_debug(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT && ret < PAGE_SIZE; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s\n", debug_commands[i].str);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/** Store callback for the @c debug_command sysfs file.
 *
 * This function is called when the @c debug_command sysfs file is written to.
 * It matches the requested command against the available commands, and if
 * a matching command is found calls the associated function from
 * @ref debug_commands to issue the command.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t issue_debug(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT; i++) {
		if (sysfs_streq(debug_commands[i].str, buf)) {
			debug_commands[i].func(kbdev);
			return count;
		}
	}

	/* Debug Command not found */
	dev_err(dev, "debug_command: command not known\n");
	return -EINVAL;
}

/** The sysfs file @c debug_command.
 *
 * This is used to issue general debug commands to the device driver.
 * Reading it will produce a list of debug commands, separated by newlines.
 * Writing to it with one of those commands will issue said command.
 */
DEVICE_ATTR(debug_command, S_IRUGO | S_IWUSR, show_debug, issue_debug);
#endif				/* CONFIG_MALI_DEBUG */

#ifdef CONFIG_MALI_TRACE_TIMELINE
/** The sysfs file @c timeline_defs.
 *
 * This provides formatting for the timeline trace system.
 */
DEVICE_ATTR(timeline_defs, S_IRUGO, show_timeline_defs, NULL);
#endif

static int kbase_common_reg_map(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	int err = -ENOMEM;

	osdev->reg_res = request_mem_region(osdev->reg_start, osdev->reg_size, dev_name(osdev->dev));
	if (!osdev->reg_res) {
		dev_err(osdev->dev, "Register window unavailable\n");
		err = -EIO;
		goto out_region;
	}

	osdev->reg = ioremap(osdev->reg_start, osdev->reg_size);
	if (!osdev->reg) {
		dev_err(osdev->dev, "Can't remap register window\n");
		err = -EINVAL;
		goto out_ioremap;
	}

	return 0;

 out_ioremap:
	release_resource(osdev->reg_res);
	kfree(osdev->reg_res);
 out_region:
	return err;
}

static void kbase_common_reg_unmap(kbase_device * const kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;

	iounmap(osdev->reg);
	release_resource(osdev->reg_res);
	kfree(osdev->reg_res);
}

static int kbase_common_device_init(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	int err = -ENOMEM;
	mali_error mali_err;
	enum {
		inited_mem = (1u << 0),
		inited_job_slot = (1u << 1),
		inited_pm = (1u << 2),
		inited_js = (1u << 3),
		inited_irqs = (1u << 4)
		    , inited_debug = (1u << 5)
		    , inited_js_softstop = (1u << 6)
#if MALI_CUSTOMER_RELEASE == 0
		    , inited_js_timeouts = (1u << 7)
#endif
		    /* BASE_HW_ISSUE_8401 */
		    , inited_workaround = (1u << 8)
		    , inited_pm_runtime_init = (1u << 9)
		    , inited_gpu_memory = (1u << 10)
#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
		,inited_sc_split        = (1u << 11)
#endif
#ifdef CONFIG_MALI_TRACE_TIMELINE
		,inited_timeline = (1u << 12)
#endif
	};

	int inited = 0;

	dev_set_drvdata(osdev->dev, kbdev);

	osdev->mdev.minor = MISC_DYNAMIC_MINOR;
	osdev->mdev.name = osdev->devname;
	osdev->mdev.fops = &kbase_fops;
	osdev->mdev.parent = get_device(osdev->dev);
	osdev->mdev.mode = 0666;

	scnprintf(osdev->devname, DEVNAME_SIZE, "%s%d", kbase_drv_name, kbase_dev_nr++);

	if (misc_register(&osdev->mdev)) {
		dev_err(osdev->dev, "Couldn't register misc dev %s\n", osdev->devname);
		err = -EINVAL;
		goto out_misc;
	}

	if (device_create_file(osdev->dev, &dev_attr_power_policy)) {
		dev_err(osdev->dev, "Couldn't create power_policy sysfs file\n");
		goto out_file;
	}
	/* dvfs on / off */
	if (device_create_file(osdev->dev, &dev_attr_dvfs_on))
	{
		dev_err(osdev->dev, "Couldn't create dvfs_on sysfs file\n");
		goto out_file;
	}

	/* check dvfs level */
	if (device_create_file(osdev->dev, &dev_attr_dvfs_level))
	{
		dev_err(osdev->dev, "Couldn't create dvfs_level sysfs file\n");
		goto out_file;
	}

	/* asv on / off */
	if (device_create_file(osdev->dev, &dev_attr_avs_on)) /* W0000119450 */
	{
		dev_err(osdev->dev, "Couldn't create avs_on sysfs file\n"); /* W0000119450 */
		goto out_file;
	}

	
	/* dvfs print on / off */
	if (device_create_file(osdev->dev, &dev_attr_dvfs_print))
	{
		dev_err(osdev->dev, "Couldn't create dvfs_print sysfs file\n");
		goto out_file;
	}

	
	/* thermal throttle on / off */
	if (device_create_file(osdev->dev, &dev_attr_thermal_throttle_on))
	{
		dev_err(osdev->dev, "Couldn't create thermal_throttle_on sysfs file\n");
		goto out_file;
	}


	/* cpulock_threshold */
	if (device_create_file(osdev->dev, &dev_attr_cpu_lock_value))
	{
		dev_err(osdev->dev, "Couldn't create dev_attr_cpulock_threshold sysfs file\n");
		goto out_file;
	}

	/* cpu_unlock_threshold*/
	if (device_create_file(osdev->dev, &dev_attr_cpu_unlock_value))
	{
		dev_err(osdev->dev, "Couldn't create dev_attr_cpu_unlock_threshold sysfs file\n");
		goto out_file;
	}

	
	/* dvfs_maxfreq_limit on / off */
	if (device_create_file(osdev->dev, &dev_attr_dvfs_maxfreq_limit))
	{
		dev_err(osdev->dev, "Couldn't create dvfs_maxfreq_limit sysfs file\n");
		goto out_file;
	}


	/* dvfs_minfreq_lock on / off */
	if (device_create_file(osdev->dev, &dev_attr_dvfs_minfreq_lock))
	{
		dev_err(osdev->dev, "Couldn't create dvfs_minfreq_lock sysfs file\n");
		goto out_file;
	}
	

	/* Voltage table control */
	if (device_create_file(osdev->dev, &dev_attr_voltage_change))
	{
		dev_err(osdev->dev, "Couldn't create voltage_change sysfs file\n");
		goto out_file;
	}

	down(&kbase_dev_list_lock);
	list_add(&osdev->entry, &kbase_dev_list);
	up(&kbase_dev_list_lock);
	dev_info(osdev->dev, "Probed as %s\n", dev_name(osdev->mdev.this_device));

	mali_err = kbase_pm_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_pm;

	if (kbdev->pm.callback_power_runtime_init) {
		mali_err = kbdev->pm.callback_power_runtime_init(kbdev);
		if (MALI_ERROR_NONE != mali_err)
			goto out_partial;

		inited |= inited_pm_runtime_init;
	}

	mali_err = kbase_mem_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_mem;

	mali_err = kbase_job_slot_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_job_slot;

	mali_err = kbasep_js_devdata_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_js;

	err = kbase_install_interrupts(kbdev);
	if (err)
		goto out_partial;

	inited |= inited_irqs;

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	if (device_create_file(osdev->dev, &dev_attr_sc_split))
	{
		dev_err(osdev->dev, "Couldn't create sc_split sysfs file\n");
		goto out_partial;
	}

	inited |= inited_sc_split;
#endif 

	if (device_create_file(osdev->dev, &dev_attr_gpu_memory)) {
		dev_err(osdev->dev, "Couldn't create gpu_memory sysfs file\n");
		goto out_partial;
	}
	inited |= inited_gpu_memory;
#ifdef CONFIG_MALI_DEBUG

	if (device_create_file(osdev->dev, &dev_attr_debug_command)) {
		dev_err(osdev->dev, "Couldn't create debug_command sysfs file\n");
		goto out_partial;
	}
	inited |= inited_debug;

	if (device_create_file(osdev->dev, &dev_attr_js_softstop_always)) {
		dev_err(osdev->dev, "Couldn't create js_softstop_always sysfs file\n");
		goto out_partial;
	}
	inited |= inited_js_softstop;
#endif				/* CONFIG_MALI_DEBUG */

#if MALI_CUSTOMER_RELEASE == 0
	if (device_create_file(osdev->dev, &dev_attr_js_timeouts)) {
		dev_err(osdev->dev, "Couldn't create js_timeouts sysfs file\n");
		goto out_partial;
	}
	inited |= inited_js_timeouts;
#endif				/* MALI_CUSTOMER_RELEASE */

#ifdef CONFIG_MALI_TRACE_TIMELINE
	if (device_create_file(osdev->dev, &dev_attr_timeline_defs)) {
		dev_err(osdev->dev, "Couldn't create timeline_defs sysfs file\n");
		goto out_partial;
	}
	inited |= inited_timeline;
#endif				/* MALI_CUSTOMER_RELEASE */

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8401)) {
		if (MALI_ERROR_NONE != kbasep_8401_workaround_init(kbdev))
			goto out_partial;

		inited |= inited_workaround;
	}

	mali_err = kbase_pm_powerup(kbdev);
	if (MALI_ERROR_NONE == mali_err) {
#ifdef CONFIG_MALI_DEBUG
#ifndef CONFIG_MALI_NO_MALI
		if (MALI_ERROR_NONE != kbasep_common_test_interrupt_handlers(kbdev)) {
			dev_err(osdev->dev, "Interrupt assigment check failed.\n");
			err = -EINVAL;
			goto out_partial;
		}
#endif				/* CONFIG_MALI_NO_MALI */
#endif				/* CONFIG_MALI_DEBUG */

		/* intialise the kctx list */
		mutex_init(&kbdev->kctx_list_lock);
		INIT_LIST_HEAD(&kbdev->kctx_list);
		return 0;
	}

 out_partial:
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8401)) {
		if (inited & inited_workaround)
			kbasep_8401_workaround_term(kbdev);
	}
#ifdef CONFIG_MALI_TRACE_TIMELINE
	if (inited & inited_timeline)
		device_remove_file(kbdev->osdev.dev, &dev_attr_timeline_defs);
#endif
#if MALI_CUSTOMER_RELEASE == 0
	if (inited & inited_js_timeouts)
		device_remove_file(kbdev->osdev.dev, &dev_attr_js_timeouts);
#endif
#ifdef CONFIG_MALI_DEBUG
	if (inited & inited_js_softstop)
		device_remove_file(kbdev->osdev.dev, &dev_attr_js_softstop_always);

	if (inited & inited_debug)
		device_remove_file(kbdev->osdev.dev, &dev_attr_debug_command);

#endif				/* CONFIG_MALI_DEBUG */
	if (inited & inited_gpu_memory)
		device_remove_file(kbdev->osdev.dev, &dev_attr_gpu_memory);

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	if (inited & inited_sc_split)
	{
		device_remove_file(kbdev->osdev.dev, &dev_attr_sc_split);
	}
#endif

	if (inited & inited_js)
		kbasep_js_devdata_halt(kbdev);

	if (inited & inited_job_slot)
		kbase_job_slot_halt(kbdev);

	if (inited & inited_mem)
		kbase_mem_halt(kbdev);

	if (inited & inited_pm)
		kbase_pm_halt(kbdev);

	if (inited & inited_irqs)
		kbase_release_interrupts(kbdev);

	if (inited & inited_js)
		kbasep_js_devdata_term(kbdev);

	if (inited & inited_job_slot)
		kbase_job_slot_term(kbdev);

	if (inited & inited_mem)
		kbase_mem_term(kbdev);

	if (inited & inited_pm_runtime_init) {
		if (kbdev->pm.callback_power_runtime_term)
			kbdev->pm.callback_power_runtime_term(kbdev);
	}

	if (inited & inited_pm)
		kbase_pm_term(kbdev);

	down(&kbase_dev_list_lock);
	list_del(&osdev->entry);
	up(&kbase_dev_list_lock);

	device_remove_file(kbdev->osdev.dev, &dev_attr_power_policy);
 out_file:
	misc_deregister(&kbdev->osdev.mdev);
 out_misc:
	put_device(osdev->dev);
	return err;
}

static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device *kbdev;
	struct kbase_os_device *osdev;
	struct resource *reg_res;
	kbase_attribute *platform_data;
	int err;
	int i;
	struct mali_base_gpu_core_props *core_props;
#ifdef CONFIG_MALI_NO_MALI
	mali_error mali_err;
#endif				/* CONFIG_MALI_NO_MALI */

	kbdev = kbase_device_alloc();
	if (!kbdev) {
		dev_err(&pdev->dev, "Can't allocate device\n");
		err = -ENOMEM;
		goto out;
	}
#ifdef CONFIG_MALI_NO_MALI
	mali_err = midg_device_create(kbdev);
	if (MALI_ERROR_NONE != mali_err) {
		dev_err(&pdev->dev, "Can't initialize dummy model\n");
		err = -ENOMEM;
		goto out_midg;
	}
#endif				/* CONFIG_MALI_NO_MALI */

	osdev = &kbdev->osdev;
	osdev->dev = &pdev->dev;
	platform_data = (kbase_attribute *) osdev->dev->platform_data;

	if (NULL == platform_data) {
		dev_err(osdev->dev, "Platform data not specified\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	if (MALI_TRUE != kbasep_validate_configuration_attributes(kbdev, platform_data)) {
		dev_err(osdev->dev, "Configuration attributes failed to validate\n");
		err = -EINVAL;
		goto out_free_dev;
	}
	kbdev->config_attributes = platform_data;

	/* 3 IRQ resources */
	for (i = 0; i < 3; i++) {
		struct resource *irq_res;

		irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq_res) {
			dev_err(osdev->dev, "No IRQ resource at index %d\n", i);
			err = -ENOENT;
			goto out_free_dev;
		}

		osdev->irqs[i].irq = irq_res->start;
		osdev->irqs[i].flags = (irq_res->flags & IRQF_TRIGGER_MASK);
	}

	/* the first memory resource is the physical address of the GPU registers */
	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!reg_res) {
		dev_err(&pdev->dev, "Invalid register resource\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	osdev->reg_start = reg_res->start;
	osdev->reg_size = resource_size(reg_res);

	err = kbase_common_reg_map(kbdev);
	if (err)
		goto out_free_dev;

	/* setup the mem allocator for pre-allocated kernel memory */
	kbase_carveout_init(kbdev);

	if (MALI_ERROR_NONE != kbase_device_init(kbdev)) {
		dev_err(&pdev->dev, "Can't initialize device\n");
		err = -ENOMEM;
		goto out_reg_unmap;
	}
#ifdef CONFIG_UMP
	kbdev->memdev.ump_device_id = kbasep_get_config_value(kbdev, platform_data, KBASE_CONFIG_ATTR_UMP_DEVICE);
#endif				/* CONFIG_UMP */

	kbdev->memdev.per_process_memory_limit = kbasep_get_config_value(kbdev, platform_data, KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT);

	/* obtain min/max configured gpu frequencies */
	core_props = &(kbdev->gpu_props.props.core_props);
	core_props->gpu_freq_khz_min = kbasep_get_config_value(kbdev, platform_data, KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN);
	core_props->gpu_freq_khz_max = kbasep_get_config_value(kbdev, platform_data, KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX);
	kbdev->gpu_props.irq_throttle_time_us = kbasep_get_config_value(kbdev, platform_data, KBASE_CONFIG_ATTR_GPU_IRQ_THROTTLE_TIME_US);

	err = kbase_common_device_init(kbdev);
	if (err) {
		dev_err(osdev->dev, "Failed kbase_common_device_init\n");
		goto out_term_dev;
	}
	return 0;

 out_term_dev:
	kbase_device_term(kbdev);
 out_reg_unmap:
	kbase_common_reg_unmap(kbdev);
 out_free_dev:
#ifdef CONFIG_MALI_NO_MALI
	midg_device_destroy(kbdev);
 out_midg:
#endif				/* CONFIG_MALI_NO_MALI */
	kbase_device_free(kbdev);
 out:
	return err;
}

static int kbase_common_device_remove(struct kbase_device *kbdev)
{
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8401))
		kbasep_8401_workaround_term(kbdev);

	if (kbdev->pm.callback_power_runtime_term)
		kbdev->pm.callback_power_runtime_term(kbdev);

	/* Remove the sys power policy file */
	device_remove_file(kbdev->osdev.dev, &dev_attr_power_policy);

#ifdef CONFIG_MALI_TRACE_TIMELINE
	device_remove_file(kbdev->osdev.dev, &dev_attr_timeline_defs);
#endif

#ifdef CONFIG_MALI_DEBUG
	device_remove_file(kbdev->osdev.dev, &dev_attr_js_softstop_always);
	device_remove_file(kbdev->osdev.dev, &dev_attr_debug_command);
#endif				/* CONFIG_MALI_DEBUG */
	device_remove_file(kbdev->osdev.dev, &dev_attr_gpu_memory);

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	device_remove_file(kbdev->osdev.dev, &dev_attr_sc_split);
#endif
	device_remove_file(kbdev->osdev.dev, &dev_attr_dvfs_on);
	device_remove_file(kbdev->osdev.dev, &dev_attr_dvfs_level);
	device_remove_file(kbdev->osdev.dev, &dev_attr_avs_on); 
	device_remove_file(kbdev->osdev.dev, &dev_attr_dvfs_print);
	device_remove_file(kbdev->osdev.dev, &dev_attr_thermal_throttle_on);
	device_remove_file(kbdev->osdev.dev, &dev_attr_cpu_lock_value);
	device_remove_file(kbdev->osdev.dev, &dev_attr_cpu_unlock_value);
	device_remove_file(kbdev->osdev.dev, &dev_attr_dvfs_maxfreq_limit);	
	device_remove_file(kbdev->osdev.dev, &dev_attr_dvfs_minfreq_lock);
	device_remove_file(kbdev->osdev.dev, &dev_attr_voltage_change);

	kbasep_js_devdata_halt(kbdev);
	kbase_job_slot_halt(kbdev);
	kbase_mem_halt(kbdev);
	kbase_pm_halt(kbdev);

	kbase_release_interrupts(kbdev);

	kbasep_js_devdata_term(kbdev);
	kbase_job_slot_term(kbdev);
	kbase_mem_term(kbdev);
	kbase_pm_term(kbdev);

	down(&kbase_dev_list_lock);
	list_del(&kbdev->osdev.entry);
	up(&kbase_dev_list_lock);

	misc_deregister(&kbdev->osdev.mdev);
	put_device(kbdev->osdev.dev);
	kbase_common_reg_unmap(kbdev);
	kbase_device_term(kbdev);
#ifdef CONFIG_MALI_NO_MALI
	midg_device_destroy(kbdev);
#endif				/* CONFIG_MALI_NO_MALI */
	kbase_device_free(kbdev);

	return 0;
}

static int kbase_platform_device_remove(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_common_device_remove(kbdev);
}

/** Suspend callback from the OS.
 *
 * This is called by Linux when the device should suspend.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */

#ifndef CONFIG_PM
static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_pm_suspend(kbdev);
	return 0;
}

/** Resume callback from the OS.
 *
 * This is called by Linux when the device should resume from suspension.
 *
 * @param dev  The device to resume
 *
 * @return A standard Linux error code
 */
static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_pm_resume(kbdev);
	return 0;
}

#endif 
/** Runtime suspend callback from the OS.
 *
 * This is called by Linux when the device should prepare for a condition in which it will
 * not be able to communicate with the CPU(s) and RAM due to power management.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */
#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM)
static int kbase_device_runtime_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbdev->pm.callback_power_runtime_off) {
		kbdev->pm.callback_power_runtime_off(kbdev);
		KBASE_DEBUG_PRINT_INFO(KBASE_PM, "runtime suspend\n");
		KBASE_DEBUG_PRINT(KBASE_MEM, "[%s] : MALI KBASE SUSPEND\n", __FUNCTION__);
	}
	return 0;
}
#endif				/* CONFIG_PM_RUNTIME */

/** Runtime resume callback from the OS.
 *
 * This is called by Linux when the device should go into a fully active state.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM)
int kbase_device_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbdev->pm.callback_power_runtime_on) {
		ret = kbdev->pm.callback_power_runtime_on(kbdev);
		KBASE_DEBUG_PRINT_INFO(KBASE_PM, "runtime resume\n");
		KBASE_DEBUG_PRINT(KBASE_MEM, "[%s] : MALI KBASE RESUME\n", __FUNCTION__);
	}
	return ret;
}
#endif				/* CONFIG_PM_RUNTIME */

/** Runtime idle callback from the OS.
 *
 * This is called by Linux when the device appears to be inactive and it might be
 * placed into a low power state
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */

#ifdef CONFIG_PM_RUNTIME
static int kbase_device_runtime_idle(struct device *dev)
{
	/* Avoid pm_runtime_suspend being called */
	return 1;
}
#endif				/* CONFIG_PM_RUNTIME */

/** The power management operations for the platform driver.
 */
static const struct dev_pm_ops kbase_pm_ops = {
#if defined (CONFIG_PM)
	SET_SYSTEM_SLEEP_PM_OPS(kbase_device_runtime_suspend, kbase_device_runtime_resume)
#else
	.suspend = kbase_device_suspend,
	.resume = kbase_device_resume,
#endif
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = kbase_device_runtime_suspend,
	.runtime_resume = kbase_device_runtime_resume,
	.runtime_idle = kbase_device_runtime_idle,
#endif				/* CONFIG_PM_RUNTIME */
};

static struct platform_driver kbase_platform_driver = {
	.probe = kbase_platform_device_probe,
	.remove = kbase_platform_device_remove,
	.driver = {
		   .name = kbase_drv_name,
		   .owner = THIS_MODULE,
		   .pm = &kbase_pm_ops,
		   },
};

#ifdef CONFIG_MALI_PLATFORM_FAKE
static struct platform_device *mali_device;
#endif				/* CONFIG_MALI_PLATFORM_FAKE */

static int __init kbase_driver_init(void)
{
	int err;
#ifdef CONFIG_MALI_PLATFORM_FAKE
	kbase_platform_config *config;
	int attribute_count;
	struct resource resources[PLATFORM_CONFIG_RESOURCE_COUNT];
#ifndef FOX_MALI_ORG
	KBASE_DEBUG_PRINT(KBASE_MEM, "<-- MALI DDK VERSION : %s -->\n", MALI_RELEASE_NAME );
#endif

#if 1 
kds_initialize_dma_buf();	
dmabuf_kds_init();	
#endif
	config = kbasep_get_platform_config();
	attribute_count = kbasep_get_config_attribute_count(config->attributes);
#ifdef CONFIG_MACH_MANTA
	err = platform_device_add_data(&exynos5_device_g3d, config->attributes, attribute_count * sizeof(config->attributes[0]));
	if (err)
		return err;
#else

	mali_device = platform_device_alloc(kbase_drv_name, 0);
	if (mali_device == NULL)
		return -ENOMEM;

	kbasep_config_parse_io_resources(config->io_resources, resources);
	err = platform_device_add_resources(mali_device, resources, PLATFORM_CONFIG_RESOURCE_COUNT);
	if (err) {
		platform_device_put(mali_device);
		mali_device = NULL;
		return err;
	}

	err = platform_device_add_data(mali_device, config->attributes, attribute_count * sizeof(config->attributes[0]));
	if (err) {
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}

	err = platform_device_add(mali_device);
	if (err) {
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}
#endif				/* CONFIG_CONFIG_MACH_MANTA */
#endif				/* CONFIG_MALI_PLATFORM_FAKE */
	err = platform_driver_register(&kbase_platform_driver);
	if (err)
		return err;

	return 0;
}

static void __exit kbase_driver_exit(void)
{
	platform_driver_unregister(&kbase_platform_driver);

	dmabuf_kds_exit();
#ifdef CONFIG_MALI_PLATFORM_FAKE
	if (mali_device)
		platform_device_unregister(mali_device);
#endif				/* CONFIG_MALI_PLATFORM_FAKE */
}

late_initcall_sync(kbase_driver_init);
module_exit(kbase_driver_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(MALI_RELEASE_NAME);

#ifdef CONFIG_MALI_GATOR_SUPPORT
/* Create the trace points (otherwise we just get code to call a tracepoint) */
#define CREATE_TRACE_POINTS
#include "mali_linux_trace.h"

void kbase_trace_mali_pm_status(u32 event, u64 value)
{
	trace_mali_pm_status(event, value);
}

void kbase_trace_mali_pm_power_off(u32 event, u64 value)
{
	trace_mali_pm_power_off(event, value);
}

void kbase_trace_mali_pm_power_on(u32 event, u64 value)
{
	trace_mali_pm_power_on(event, value);
}

void kbase_trace_mali_job_slots_event(u32 event, const kbase_context *kctx)
{
	trace_mali_job_slots_event(event, (kctx != NULL ? kctx->osctx.tgid : 0), 0);
}

void kbase_trace_mali_page_fault_insert_pages(int event, u32 value)
{
	trace_mali_page_fault_insert_pages(event, value);
}

void kbase_trace_mali_mmu_as_in_use(int event)
{
	trace_mali_mmu_as_in_use(event);
}

void kbase_trace_mali_mmu_as_released(int event)
{
	trace_mali_mmu_as_released(event);
}

void kbase_trace_mali_total_alloc_pages_change(long long int event)
{
	trace_mali_total_alloc_pages_change(event);
}
#endif				/* CONFIG_MALI_GATOR_SUPPORT */
