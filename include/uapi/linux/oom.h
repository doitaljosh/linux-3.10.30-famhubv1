#ifndef _UAPI__INCLUDE_LINUX_OOM_H
#define _UAPI__INCLUDE_LINUX_OOM_H

/*
 * /proc/<pid>/oom_score_adj set to OOM_SCORE_ADJ_MIN disables oom killing for
 * pid.
 */
#define OOM_SCORE_ADJ_MIN	(-1000)
#define OOM_SCORE_ADJ_MAX	1000
#ifdef CONFIG_SLP_LOWMEM_NOTIFY_VIPAPP
#define IS_NOT_VIP 0
#define IS_VIP 1
#define LENGTH_OF_VIP 2
#endif
#ifdef CONFIG_SLP_LOWMEM_NOTIFY_PRELOAD
/* writing -18 sets oom_adj to -17 and prevents changing it afterwards */
#define OOM_DISABLE_FOREVER (-18)
#endif
/*
 * /proc/<pid>/oom_adj set to -17 protects from the oom killer for legacy
 * purposes.
 */
#define OOM_DISABLE (-17)
/* inclusive */
#define OOM_ADJUST_MIN (-16)
#define OOM_ADJUST_MAX 15

#endif /* _UAPI__INCLUDE_LINUX_OOM_H */
