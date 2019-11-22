/* linux/arch/arm/plat-sdp/sdp_asv.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * SDP - ASV(Adaptive Supply Voltage) driver
 *
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/power/sdp_asv.h>
#include <linux/notifier.h>
#include <linux/suspend.h>

#include <mach/soc.h>
#include <mach/map.h>

#ifdef CONFIG_SDP_THERMAL
#include <mach/sdp_thermal.h>
#endif

struct sdp_asv_info *asv_info;

static struct workqueue_struct *delayed_asv_wq;
struct delayed_work delayed_avs;

#if defined(CONFIG_SDP1304_AVS)
#include "sdp1304_asv.c"
#elif defined(CONFIG_SDP1307_AVS)
#include "sdp1307_asv.c"
#elif defined(CONFIG_SDP1404_AVS)
#include "sdp1404_asv.c"
#elif defined(CONFIG_SDP1406_AVS)
#include "sdp1406_asv.c"
#else
#warning "select SoC type for AVS."
#endif

static BLOCKING_NOTIFIER_HEAD(asv_chain_head);

int register_sdp_asv_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&asv_chain_head, nb);
}
EXPORT_SYMBOL(register_sdp_asv_notifier);

int unregister_sdp_asv_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&asv_chain_head, nb);
}
EXPORT_SYMBOL(unregister_sdp_asv_notifier);

int sdp_asv_notifier_call_chain(unsigned long val, struct sdp_asv_info *info)
{
	int ret;
	
	ret = blocking_notifier_call_chain(&asv_chain_head, val, (void *)info);

	return notifier_to_errno(ret);
}

struct sdp_asv_info * get_sdp_asv_info(void)
{
	return asv_info;
}
EXPORT_SYMBOL(get_sdp_asv_info);

static int sdp_asv_pm_notifier(struct notifier_block *notifier,
			       unsigned long pm_event, void *v)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pr_info("AVS: PM_SUSPEND_PREPARE\n");
		if (asv_info->suspend)
			asv_info->suspend(asv_info);
		
		break;

	case PM_POST_SUSPEND:
		pr_info("AVS: PM_POST_SUSPEND\n");
		if (asv_info->resume)
			asv_info->resume(asv_info);
		
		break;
	}
	
	return NOTIFY_OK;
}

static struct notifier_block sdp_asv_nb = {
	.notifier_call = sdp_asv_pm_notifier,
};

#ifdef CONFIG_SDP_THERMAL
static int sdp_asv_tmu_notifier(struct notifier_block *notifier,
			       unsigned long event, void *v)
{
	switch (event) {
	case SDP_TMU_AVS_OFF:
		pr_info("AVS: SDP_TMU_AVS_OFF\n");
		/* cpu and gpu */
		sdp_asv_notifier_call_chain(SDP_ASV_NOTIFY_AVS_OFF, asv_info);

		/* core */
		if (asv_info->avs_on)
			asv_info->avs_on(asv_info, false);

		asv_info->is_avs_on = false;
		
		break;
		
	case SDP_TMU_AVS_ON:
		pr_info("AVS: SDP_TMU_AVS_ON\n");
		/* cpu and gpu */
		sdp_asv_notifier_call_chain(SDP_ASV_NOTIFY_AVS_ON, asv_info);
		
		/* core */
		if (asv_info->avs_on)
			asv_info->avs_on(asv_info, true);

		asv_info->is_avs_on = true;
		
		break;
		
	default:
		break;	
	}
	
	return NOTIFY_OK;
}

static struct notifier_block sdp_asv_tmu_nb = {
	.notifier_call = sdp_asv_tmu_notifier,
};
#endif

static void sdp_handler_delayed_avs(struct work_struct *work)
{
	pr_info("AVS: delayed AVS_ON\n");
	
	/* cpu and gpu */
	sdp_asv_notifier_call_chain(SDP_ASV_NOTIFY_AVS_ON, asv_info);
	
	/* core, mp, us */
	if (asv_info->avs_on)
		asv_info->avs_on(asv_info, true);

	asv_info->is_avs_on = true;
}

static void create_delayed_avs_wq(void)
{
	delayed_asv_wq = create_freezable_workqueue("asv_wp");
	if (!delayed_asv_wq) {
		pr_err("AVS: error - Creation of delayed_asv_wq failed\n");
		return;
	}

	/* To support delayed avs */
	INIT_DELAYED_WORK(&delayed_avs, sdp_handler_delayed_avs);

	/* initialize tmu_state */
	queue_delayed_work_on(0, delayed_asv_wq, &delayed_avs,
				usecs_to_jiffies(4000000));
}

#if defined(CONFIG_SDP_THERMAL)
static const struct of_device_id thermal_match[] __initconst = {
	{ .compatible = "samsung,sdp-thermal" },
	{ /* sentinel */ },
};
#endif

static int __init sdp_asv_init(void)
{
	int ret = 0;

	asv_info = kzalloc(sizeof(struct sdp_asv_info), GFP_KERNEL);
	if (!asv_info) {
		pr_err("AVS: ERROR - failed to allocate asv info\n");
		goto out1;
	}

	pr_info("AVS: Adaptive Support Voltage init\n");

#if defined(CONFIG_SDP1304_AVS)
	ret = sdp1304_asv_init(asv_info);
#elif defined(CONFIG_SDP1307_AVS)
	ret = sdp1307_asv_init(asv_info);
#elif defined(CONFIG_SDP1404_AVS)
	ret = sdp1404_asv_init(asv_info);
#elif defined(CONFIG_SDP1406_AVS)
	ret = sdp1406_asv_init(asv_info);
#else
	ret = -EIO;
#endif
	if (ret) {
		pr_err("%s: avs init error %d\n", __func__, ret);
		goto out2;
	}

	/* AVS is default off status */
	asv_info->is_avs_on = false;

	/* get CPU ids & tmcb */
	if (asv_info->get_cpu_ids_tmcb) {
		if (asv_info->get_cpu_ids_tmcb(asv_info))
			pr_err("AVS: failed to get CPU ids and tmcb\n");
	}

	/* get GPU ids & tmcb */
	if (asv_info->get_gpu_ids_tmcb) {
		if (asv_info->get_gpu_ids_tmcb(asv_info))
			pr_err("AVS: failed to get GPU ids and tmcb\n");
	}

	/* get Core ids & tmcb */
	if (asv_info->get_core_ids_tmcb) {
		if (asv_info->get_core_ids_tmcb(asv_info))
			pr_err("AVS: failed to get Core ids and tmcb\n");
	}

	/* get MP ids & tmcb */
	if (asv_info->get_mp_ids_tmcb) {
		if (asv_info->get_mp_ids_tmcb(asv_info))
			pr_err("AVS: failed to get MP ids and tmcb\n");
	}

	/* get US ids & tmcb */
	if (asv_info->get_us_ids_tmcb) {
		if (asv_info->get_us_ids_tmcb(asv_info))
			pr_err("AVS: failed to get US ids and tmcb\n");
	}

	if (asv_info->store_result) {
		if (asv_info->store_result(asv_info)) {
			pr_err("AVS: Can not success to store result\n");
			goto out2;
		}
	} else {
		pr_info("AVS: No store_result function\n");
		goto out2;
	}

	register_pm_notifier(&sdp_asv_nb);

#ifdef CONFIG_SDP_THERMAL
	if (of_find_matching_node(NULL, thermal_match)) {
	register_sdp_tmu_notifier(&sdp_asv_tmu_nb);
	} else {
		pr_info("AVS: samsung.sdp-thermal node is not in device tree.\n");
		create_delayed_avs_wq();
	}
#else
	create_delayed_avs_wq();
#endif

	return 0;
out2:
	kfree(asv_info);
out1:
	return -EINVAL;
}
device_initcall_sync(sdp_asv_init);

