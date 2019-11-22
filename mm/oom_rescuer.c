/*
 * Implementation of a special mechanism to prevent OOM killer invocations
 * during page allocation.
 */

#include <linux/oom_rescuer.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/module.h>

static LIST_HEAD(oom_rescuer_list);
static DECLARE_RWSEM(oom_rescuer_rwsem);

/*
 * Add a OOM rescuer callback to be called from the page allocation code path.
 */
void register_oom_rescuer(struct oom_rescuer *rescuer)
{
	down_write(&oom_rescuer_rwsem);
	list_add_tail(&rescuer->list, &oom_rescuer_list);
	up_write(&oom_rescuer_rwsem);
}
EXPORT_SYMBOL(register_oom_rescuer);

/*
 * Remove one.
 */
void unregister_oom_rescuer(struct oom_rescuer *rescuer)
{
	down_write(&oom_rescuer_rwsem);
	list_del(&rescuer->list);
	up_write(&oom_rescuer_rwsem);
}
EXPORT_SYMBOL(unregister_oom_rescuer);

/*
 * Call all OOM rescuers presented in the system passing the stage code.
 */
void call_oom_rescuers(enum oom_rescuer_stage stage)
{
	struct oom_rescuer *rescuer;

	down_read(&oom_rescuer_rwsem);
	list_for_each_entry(rescuer, &oom_rescuer_list, list) {
		rescuer->rescue(rescuer, stage);
	}
	up_read(&oom_rescuer_rwsem);
}
EXPORT_SYMBOL(call_oom_rescuers);
