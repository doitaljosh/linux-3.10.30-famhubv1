#ifndef _LINUX_OOM_RESCUER_H
#define _LINUX_OOM_RESCUER_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/list.h>

enum oom_rescuer_stage {
	OOMR_LACK_OF_MEM, /* Low memory level, do something for future needs  */
	OOMR_CRITICAL,    /* Critical situation, all methods are good */
};

/*
 * A callback you can register to prevent processes from being killed by the OOM
 * killer.
 *
 * Each OOM rescuer can do something to prevent processes killing. At stage
 * OOMR_LACK_OF_MEM it tries to free memory for future needs somehow. At stage
 * OOMR_CRITICAL it does everything it counts necessary, because at this point the
 * system can kill somebody because of impossibility of page allocation.
 */
struct oom_rescuer {
	struct list_head list;
	void (*rescue)(struct oom_rescuer *rescuer, enum oom_rescuer_stage stage);
};

void register_oom_rescuer(struct oom_rescuer *);
void unregister_oom_rescuer(struct oom_rescuer *);
void call_oom_rescuers(enum oom_rescuer_stage);
#endif /* _LINUX_OOM_RESCUER_H */
