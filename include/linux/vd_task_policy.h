#ifndef _LINUX_VD_TASK_POLICY_H
#define _LINUX_VD_TASK_POLICY_H

/*Check task name entry in /proc/vd_allowed_task_list*/
int vd_policy_allow_task_check(const char *task_name);

/*Check task name entry in /proc/vd_except_task_list*/
int vd_policy_except_task_check(const char *task_name);

#endif
