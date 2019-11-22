/*
 * This is part of KUBT (kernel user backtrace).
 *
 * Sergey SENOZHATSKY, sergey1985.s@samsung.com
 */
#ifndef _KUBT_ARM_H_
#define _KUBT_ARM_H_

struct kdbgd_bt_regs;

int kubt_kdbgd_show_trace(struct mm_struct *mm, struct kdbgd_bt_regs *regs);

#endif /* _KUBT_ARM_H_ */
