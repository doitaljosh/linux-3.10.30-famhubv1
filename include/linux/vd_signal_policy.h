#ifndef _LINUX_VD_SIGNAL_POLICY_H
#define _LINUX_VD_SIGNAL_POLICY_H

/* check if task name entry is in /proc/vd_signal_policy_list */
int vd_signal_policy_check(const char *task_name);
#endif
