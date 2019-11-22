/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 *
 *
 *
 * Modified by vikram.anuj@samsung.com
 *
 *
 */
#include <linux/ioport.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/linux/mali_kbase_config_linux.h>
/*#include <ump/ump_common.h>*/

/* Set this to 1 to enable dedicated memory banks */
#define T6F1_ZBT_DDR_ENABLED 0
#define KBASE_FOXP_POWER_MANAGEMENT_CALLBACKS     ((uintptr_t)&pm_callbacks)

static int foxp_pm_callback_power_on(kbase_device *kbdev)
{
	/*
	* Nothing is needed on FoxP, but we may have destroyed GPU state
	* (if the below HARD_RESET code is active)
	*/
	return 1;
}

static void foxp_pm_callback_power_off(kbase_device *kbdev)
{
#if 1 //HARD_RESET_AT_POWER_OFF
	/* Cause a GPU hard reset to test whether we have actually idled the GPU
	* and that we properly reconfigure the GPU on power up.
	* Usually this would be dangerous, but if the GPU is working correctly it should
	* be completely safe as the GPU should not be active at this point.
	* However this is disabled normally because it will most likely interfere with
	* bus logging etc.
	*/
	//KBASE_TRACE_ADD(kbdev, CORE_GPU_HARD_RESET, NULL, NULL, 0u, 0);
	kbase_os_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_HARD_RESET);
#endif
}
static int foxp_pm_callback_runtime_on(kbase_device *kbdev)
{
	kbase_pm_clock_on(kbdev);
	return 0;
}

static void foxp_pm_callback_runtime_off(kbase_device *kbdev)
{

	kbase_pm_clock_off(kbdev);
}


static kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = foxp_pm_callback_power_on,
	.power_off_callback = foxp_pm_callback_power_off,
	.power_runtime_on_callback = foxp_pm_callback_runtime_on,
	.power_runtime_off_callback = foxp_pm_callback_runtime_off,
};


static kbase_io_resources io_resources =
{
	.job_irq_number   = 109+32,
	.mmu_irq_number   = 108+32,
	.gpu_irq_number   = 107+32,
	.io_memory_region =
	{
		.start = 0x10500000,
		.end   = 0x10500000 + (4096 * 5) - 1
	}
};

#if T6F1_ZBT_DDR_ENABLED
#if 0												/*	as of now commented assuming that ZBT is actual Dedicated DDR for mali only, which we dont have, thus defining only dedicated memory in shared DDR*/
static kbase_attribute lt_zbt_attrs[] = {
	{
		KBASE_MEM_ATTR_PERF_CPU,
		KBASE_MEM_PERF_SLOW
	},
	{
		KBASE_MEM_ATTR_END,
		0
	}
};

static kbase_memory_resource lt_zbt = {
	.base = 0xFD000000,
	.size = 16 * 1024 * 1024UL /* 16MB */,
	.attributes = lt_zbt_attrs,
	.name = "T604 ZBT memory"
};
#endif

static kbase_attribute lt_ddr_attrs[] = {
	{
		KBASE_MEM_ATTR_PERF_CPU,
		KBASE_MEM_PERF_SLOW
	},
	{
		KBASE_MEM_ATTR_END,
		0
	}
};


static kbase_memory_resource lt_ddr = {
	.base = 0x4A000000,
	.size = 50 * 1024 * 1024UL /* 256MB */,
	.attributes = lt_ddr_attrs,
	.name = "T604 DDR memory"
};

#endif /* T6F1_ZBT_DDR_ENABLED */



static kbase_attribute config_attributes[] = {
	{
		KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT,
		800 * 1024 * 1024UL /* 100MB */
	},
#if 0
	{																		/*	There is one UMP device, and we didnt connected to any other module in our platform. so only Z as of now		*/
		KBASE_CONFIG_ATTR_UMP_DEVICE,
		UMP_DEVICE_Z_SHIFT
	},
#endif
#if 1 /* Enable this for OS MEMORY */
	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX,
		800 * 1024 * 1024UL /* 100MB */
	},
	#endif
#if T6F1_ZBT_DDR_ENABLED
/*	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		(uintptr_t)&lt_zbt
	},
*/
	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		(uintptr_t)&lt_ddr
	},
#endif /* T6F1_ZBT_DDR_ENABLED */

	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU,
		KBASE_MEM_PERF_FAST
	},
	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX,
		500000
	},

	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN,
		500000
	},
	{
		KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,
		577000000 /* 0.577s: vexpress settings, scaled by clock frequency (5000/130) */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
		1 /* between 0.577s and 1.154s before soft-stop a job */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,
		133 /* 77s before hard-stop */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
		40000 /* 6.4hrs before NSS hard-stop (5000/130 times slower than vexpress) */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,
		200 /* 115s before resetting GPU */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,
		40067 /* 6.4 hrs before resetting GPU */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS,
		3000 /* 3s before cancelling stuck jobs */
	},
	
	{
		KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS,
		38000000 /* 38ms: vexpress settings, scaled by clock frequency (5000/130) */
	},
	{
		KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS,
		KBASE_FOXP_POWER_MANAGEMENT_CALLBACKS
	},	
	
	{
		KBASE_CONFIG_ATTR_END,
		0
	}
};

kbase_platform_config platform_config =
{
		.attributes                = config_attributes,
		.io_resources              = &io_resources,
		.midgard_type              = KBASE_MALI_T604
};


