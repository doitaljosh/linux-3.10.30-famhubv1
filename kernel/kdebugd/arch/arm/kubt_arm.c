/*
 * This is part of KUBT (kernel user backtrace).
 *
 * Sergey SENOZHATSKY, sergey1985.s@samsung.com
 * Himanshu Maithani, himanshu.m@samsung.com (modify to use Kdebugd Symbol manager/Demangler)
 * (using back-tracking version)
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/processor.h>

#include "kdbg_elf_sym_api.h"
#include "kubt_misc.h"
#include "kdbg-trace.h"
#include "kubt_arm.h"

/* ARM Registers */
#define ARM_FP_REGNUM 11	/* current frame address */
#define ARM_SP_REGNUM 13	/* stack pointer */
#define ARM_LR_REGNUM 14	/* return address */
#define ARM_PC_REGNUM 15	/* program counter */

#define	INSN_ERR	((unsigned int)-1)

#define IS_THUMB_ADDR(a)	((a) & 1)
#define MAKE_THUMB_ADDR(a)	((a) | 1)

#define OFFSET	128

static unsigned long kubt_thumb_expand_imm(unsigned int imm)
{
	unsigned long count = imm >> 7;

	if (count < 8) {
		switch (count / 2) {
		case 0:
			return imm & 0xff;
		case 1:
			return (imm & 0xff) | ((imm & 0xff) << 16);
		case 2:
			return ((imm & 0xff) << 8) | ((imm & 0xff) << 24);
		case 3:
			return (imm & 0xff) | ((imm & 0xff) << 8)
				| ((imm & 0xff) << 16) | ((imm & 0xff) << 24);
		default:
			printk(KERN_WARNING
					"Condition will never be reached\n");
		}
	}

	return (0x80 | (imm & 0x7f)) << (32 - count);
}

static inline unsigned long kubt_get_user(unsigned long pc, long sz)
{
	mm_segment_t fs;
	unsigned long insn = INSN_ERR;
	long ret = 0;

	if (!access_ok(VERIFY_READ, pc, sizeof(insn)))
		return 0;

	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = __copy_from_user(&insn, (void *)pc, sz);
	set_fs(fs);

	if (unlikely(ret))
		return INSN_ERR;
	return insn;
}

/* a simple wrapper to turn bad instruction into a harmless
 * NOOP (all zeros), so none of bitmask checks should pass.
 * e.g. for insn = (((unsigned int)-1) __BIT(insn, 8) check
 * will wrongly give a positive result.*/
static inline unsigned long kubt_get_insn(unsigned long pc, int sz)
{
	unsigned int insn = kubt_get_user(pc, sz);
	if (unlikely(insn == INSN_ERR))
		insn = 0;
	return insn;
}

static unsigned long kubt_get_symbol_start(struct mm_struct *tsk_mm,
		struct kubt_kdbgd_sym *sym)
{
	if (sym && sym->is_valid)
		return sym->start;
	return 0;
}

static int call_depth;
#ifdef CONFIG_ELF_MODULE
static void kubt_print_symbol(struct kubt_kdbgd_sym *sym, unsigned long addr)
{
	const char *sym_name = "??";
	const char *elf_name = "??";

	BUG_ON(!sym);

	if (sym->is_valid) {
		sym_name = sym->sym_name;
		elf_name = sym->lib_name;
	}
#ifdef CONFIG_DWARF_MODULE
	if (sym->is_valid && (sym->pdf_info->df_line_no != 0))
		pr_info(KDEBUGD_PRINT_DWARF, call_depth, addr,
				sym_name,
				sym->pdf_info->df_file_name,
				sym->pdf_info->df_line_no);
	else
#endif
		pr_info(KDEBUGD_PRINT_ELF, call_depth, addr,
				sym_name,
				elf_name);
	call_depth++;
}

static int kubt_filter_sym_name(struct mm_struct *tsk_mm,
		const char *sym_name)
{
	/* this strcmp stuff, of course, works in some cases. alternatively we
	 * can check what function address in called from ELF entry point
	 * (_start()). the main problem here is that ELF can redefine main(),
	 * to, let's say for example, MyMainFunction(). this is for sure
	 * utterly stupid but still possible. */
	if (!strcmp(sym_name, "main") ||
			!strcmp(sym_name, "__thread_start") ||
			!strcmp(sym_name, "__cxa_finalize") ||
			!strcmp(sym_name, "__libc_start_main") ||
			!strcmp(sym_name, "_dl_fini"))
		return 1;
	return 0;
}

/* check if we can resolve and print symbol. if we managed to resolve the
 * symbol, then filter its name (we stop if we found e.g. main()) and store
 * its start address */
static int kubt_lookup_sym(struct mm_struct *tsk_mm, unsigned long pc, unsigned long *start)
{
	struct kubt_kdbgd_sym sym;
	struct aop_symbol_info symbol_info;
	char *sym_name = NULL, *lib_name = NULL;
	unsigned int start_addr = 0;
#ifdef CONFIG_DWARF_MODULE
	static struct aop_df_info df_info;
#endif
	int ret = -1;

	sym_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
			KDBG_ELF_SYM_NAME_LENGTH_MAX,
			GFP_KERNEL);
	if (!sym_name)
		goto exit_free;
	sym_name[0] = '\0';

	lib_name = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
			KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN,
			GFP_KERNEL);
	if (!lib_name)
		goto exit_free;
	lib_name[0] = '\0';

	symbol_info.pfunc_name = sym_name;
	symbol_info.start_addr = 0;
#ifdef CONFIG_DWARF_MODULE
	symbol_info.df_info_flag = 1;
	symbol_info.pdf_info = &df_info;
#endif /* CONFIG_DWARF_MODULE */

	ret = kdbg_elf_get_symbol_and_lib_by_mm(tsk_mm,
			pc, lib_name, &symbol_info,
			&start_addr);
	if (ret && start_addr) {
		sym.is_valid = 1;
		sym.lib_name = lib_name;
		sym.sym_name = sym_name;
		sym.start = start_addr;
#ifdef CONFIG_DWARF_MODULE
		sym.pdf_info = &df_info;
#endif
	} else {
		sym.is_valid = 0;
	}

	/* let user know that we hit unknown symbol before we abort
	 * the mission */
	kubt_print_symbol(&sym, pc);

	if (!sym.is_valid) {
		ret =  -EINVAL;
		goto exit_free;
	}
	if (kubt_filter_sym_name(tsk_mm, sym.sym_name)) {
		ret = 1; /* filtered, stop... */
		goto exit_free;
	}

	*start = kubt_get_symbol_start(tsk_mm, &sym);
	ret = 0;

exit_free:
	if (sym_name)
		KDBG_MEM_DBG_KFREE(sym_name);
	if (lib_name)
		KDBG_MEM_DBG_KFREE(lib_name);

	return ret;
}
#else
static int kubt_lookup_sym(struct mm_struct *tsk_mm, unsigned long pc, unsigned long *start)
{
	pr_info(KDEBUGD_PRINT_ELF, call_depth, pc, "??", "??");
	return -1;
}
#endif

/* this function must be called from THUMB16 context ONLY.
 *
 * check if current THUMB16 instruction is actullay a tail of
 * THUMB32 instruction.
 */
static inline int kubt_is_32bit_tail(unsigned long pc, unsigned long start)
{
	unsigned int insn;

	if (pc - 3 < start)
		return 0;

	/* pc - IS_THUMB_ADDR(pc) - 2 */
	insn = kubt_get_insn(pc - 3, 2);
	if ((insn & 0xe000) == 0xe000 && (insn & 0x1800) != 0)
		return 1;
	return 0;
}

/* ensure that targeted area (function `start + OFFSET') contains
 * supported prologue instructions. otherwise, we cannot jump over
 * and must continue decoding. */
static int kubt_scan_prologue(unsigned long pc, unsigned long limit)
{
	int thumb_mode = IS_THUMB_ADDR(pc);
	unsigned int insn, insn2;

	while (pc < limit) {
		if (thumb_mode)
			insn = kubt_get_user(pc - thumb_mode, 2);
		else
			insn = kubt_get_user(pc, 4);

		if (insn == INSN_ERR)
			return -EINVAL;

		if (thumb_mode) {
			/* push { rlist } */
			if ((insn & 0xfe00) == 0xb400) {
				int mask = (insn & 0xff) |
					((insn & 0x100) << 6);

				if (mask & (1 << ARM_LR_REGNUM))
					return 0;
			}
			/* sub sp, #imm */
			else if ((insn & 0xff80) == 0xb080) {
				return 0;

			/*** THUMB32 instructions ***/
			/* str Rt, {sp, +/-#imm}! */
			} else if ((insn & 0xffff) == 0xf8cd) {
				insn2 = kubt_get_insn(pc + 1, 2);
				if (__BIT(insn2, 10) && __BIT(insn2, 8))
					return 0;
				pc += 2;
			/* strd Rt, Rt2, [sp, #+/-imm]{!} */
			} else if ((insn & 0xfe5f) == 0xe84d) {
				insn2 = kubt_get_insn(pc + 1, 2);
				if (__BITS(insn2, 12, 15) == ARM_LR_REGNUM)
					return 0;
				if (__BITS(insn2, 8, 11) == ARM_LR_REGNUM)
					return 0;
				if (__BIT(insn, 5) && __BIT(insn, 8))
					return 0;
				pc += 2;
			/* str{bh}.w sp,[Rn,#+/-imm]{!} */
			} else if ((insn & 0xffdf) == 0xf88d) {
				insn2 = kubt_get_insn(pc + 1, 2);
				if (__BIT(insn2, 10) && __BIT(insn2, 8))
					return 0;
				pc += 2;
			/* stmdb sp!, { rlist } */
			} else if ((insn & 0xffff) == 0xe92d) {
				insn2 = kubt_get_insn(pc + 1, 2);
				if (insn2 & (1 << ARM_LR_REGNUM))
					return 0;
				pc += 2;
			/* sub.w sp, Rn, #imm */
			} else if ((insn & 0xfbff) == 0xf1ad) {
				return 0;
			}
		/* ARM instructions */
		/* sub sp,sp, size */
		} else if ((insn & 0xfffff000) == 0xe24dd000) {
			return 0;
		/*  stmfd sp! {rlist} */
		} else if ((insn & 0xffff0000) == 0xe92d0000) {
			int mask = insn & 0xffff;

			if (mask & (1 << ARM_LR_REGNUM))
				return 0;
		}

		pc += 2;
		if (!thumb_mode)
			pc += 2;
	}

	return 1;
}

/* check if we can fast forward instruction decoding and jump to function
 * prologue area [assuming that we are currently in function epilogue,
 * avoiding constly traversal. the idea is to assume that function prologue
 * is located somewhere around `start' + OFFSET bytes.
 * return new jump pc or current pc if jump is not possible [but set
 * KUBT_FLAG_FUNC_EPILOGUE flag]*/
static inline unsigned long kubt_jumpover_pc(int *flags, unsigned long pc,
		unsigned long start)
{
	unsigned long ff_pc;

	*flags |= KUBT_FLAG_FUNC_EPILOGUE;
	if (unlikely(start == 0)) {
		pr_err("KUBT: no symbol `start' address was given\n");
		return pc;
	}

	if (unlikely(start + OFFSET < start))
		return pc;

	if (start - pc <= OFFSET)
		return pc;

	ff_pc = min_t(unsigned long, pc, start + OFFSET);
	/* check if function prologues is located at jump position */
	if (kubt_scan_prologue(start, ff_pc))
		return pc;

	if (IS_THUMB_ADDR(pc))
		return MAKE_THUMB_ADDR(ff_pc);
	return ff_pc;
}

/* handle THUMB16/THUMB32 instructions */
static inline void kubt_handle_thumb_insn(int *flags,
		struct kdbgd_bt_regs *ar, unsigned int insn, unsigned long start)
{
	unsigned int insn2;

	/* THUMB mode may contain both 16 and 32 bit instructions. We
	 * read in reverse order, because bytes are placed like this:
	 *
	 *	f8dd e010       ldr.w   lr, [sp, #16]
	 *	9a07            ldr     r2, [sp, #28]
	 *	eb0c 030e       add.w   r3, ip, lr
	 *	990a            ldr     r1, [sp, #40]   ; 0x28
	 *	9200            str     r2, [sp, #0]
	 *	4288            cmp     r0, r1
	 *	bfb8            it      lt
	 *	4608            movlt   r0, r1
	 * ->>	68dc            ldr     r4, [r3, #12]
	 *
	 * Whenever we see 32-bit THUMB instruction we need to re-read
	 * its lower 16-bit and process instruction as two dependent
	 * 16-bit instructions.
	 *
	 * However, since we a in reverse mode, ew may consider some
	 * THUMB32 instructions as THUMB16. Example:
	 *
	 * 0xb474 which has valid bytes to be considered THUMB16 push,
	 * however in some cases it belongs to `0xf8df 0xb474'
	 * instruction sequence, which is THUMB32 instruction */

	/*** THUMB16 instructions ***/
	if ((insn & 0xfe00) == 0xb400) {
		/* push { rlist } */
		/* __BITS 0-7 contain a mask for registers R0-R7.
		 * Bit 8 says whether to save LR (R14).*/
		int mask, rn = 0;

		if (kubt_is_32bit_tail(ar->pc, start))
			return;

		/* registers = '0':M:’000000’:register_list */
		insn = insn & 0xffff;
		mask = (insn & 0xff) | ((insn & 0x100) << 6);
		for (; rn <= ARM_PC_REGNUM; rn++) {
			if (!(mask & (1 << rn)))
				continue;
			if (rn == ARM_LR_REGNUM)
				ar->lr = kubt_get_user(ar->sp, 4);
			ar->sp += 4;
		}
		if (mask & (1 << ARM_LR_REGNUM))
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
	} else  if ((insn & 0xff00) == 0xbd00) {
		/* pop { rlist } */
		int mask, rn = 0;

		if (kubt_is_32bit_tail(ar->pc, start))
			return;

		/* registers = P:’0000000’:register_list */
		insn = insn & 0xffff;
		mask = (insn & 0xff) | ((insn & 0x100) << 7);
		/* reglist can only include the Lo registers and the pc */
		if (mask & (1 << ARM_PC_REGNUM)) {
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
		} else {
			mask = __BITS(insn, 0, 7);

			if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
				return;

			for (; rn <= ARM_PC_REGNUM; rn++) {
				if (mask & (1 << rn))
					ar->sp -= 4;
			}
		}
	} else if ((insn & 0xff80) == 0xb080) {
		/* sub sp, #imm */
		if (kubt_is_32bit_tail(ar->pc, start))
			return;
		ar->sp += ((insn & 0x7f) << 2);
	} else if ((insn & 0xff80) == 0xb000) {
		/* add sp, #imm */
		if (kubt_is_32bit_tail(ar->pc, start))
			return;
		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;
		ar->sp -= ((insn & 0x7f) << 2);
	/*** THUMB32 instructions ***/
	} else if ((insn & 0xff7f) == 0xe96d) {
		/* strd Rt, Rt2, [sp, #+/-imm]{!} */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if (__BITS(insn2, 12, 15) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(ar->sp, 4);
		if (__BITS(insn2, 8, 11) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(ar->sp + 4, 4);
		ar->sp += __BITS(insn2, 0, 7) << 2;
	} else if ((insn & 0xff7f) == 0xe94d) {
		/* strd Rt, Rt2, [sp, #+/-imm] */
		unsigned long addr = ar->sp;
		unsigned long offt = ar->sp;

		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if (__BIT(insn, 7))
			offt += __BITS(insn2, 0, 7) << 2;
		else
			offt -= __BITS(insn2, 0, 7) << 2;

		if (__BIT(insn, 8))
			addr = offt;

		if (__BITS(insn2, 12, 15) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(addr, 4);
		if (__BITS(insn2, 8, 11) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(addr + 4, 4);
	} else if ((insn & 0xffff) == 0xf84d) {
		/* STR<c>.w <Rt>,[<Rn>,#+/-<imm8>]! */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if (__BITS(insn2, 12, 15) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(ar->sp, 4);

		ar->sp += __BITS(insn2, 0, 5);
	} else if ((insn & 0xffdf) == 0xf88d) {
		/* str{bh}.w sp,[Rn,#+/-imm]{!} */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		/* Pre-indexed: index==TRUE, wback==TRUE */
		if (!(__BIT(insn2, 10) && __BIT(insn2, 8)))
			return;
		if ((insn2 & 0x0d00) == 0x0c00)
			ar->sp += __BITS(insn2, 0, 7);
		else
			ar->sp -= __BITS(insn2, 0, 7);
	} else if ((insn & 0xfe7f) == 0xe82d) {
		/*  stm{db} sp!, { rlist } */
		int rn = 0;

		/* pc - IS_THUMB_ADDR(pc) + 2 */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		for (; rn <= ARM_LR_REGNUM; rn++) {
			if (!(insn2 & (1 << rn)))
				continue;
			if (rn == ARM_LR_REGNUM)
				ar->lr = kubt_get_user(ar->sp, 4);
			ar->sp += 4;
		}
		if (insn2 & (1 << ARM_LR_REGNUM))
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
	} else if ((insn & 0xffff) == 0xe8bd) {
		/* ldmia can be detected as `(insn & 0xffd0) == 0xe890', but
		 * we are insterested only in sp! case, which has Rn set to
		 * 1101 */
		/* ldmia sp!, { rlist } */
		int rn = 0;

		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if ((insn2 & (1 << ARM_PC_REGNUM)) ||
				(insn2 & (1 << ARM_FP_REGNUM))) {
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
		} else {
			if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
				return;

			for (; rn <= ARM_PC_REGNUM; rn++) {
				if (insn2 & (1 << rn))
					ar->sp -= 4;
			}
		}
	} else if ((insn & 0xfe5f) == 0xe85d) {
		/* LDRD<c> <Rt>,<Rt2>,[<Rn>{,#+/-<imm>}]
		 * LDRD<c> <Rt>,<Rt2>,[<Rn>],#+/-<imm>
		 * LDRD<c> <Rt>,<Rt2>,[<Rn>,#+/-<imm>]!
		 **/
		int rn;

		insn2 = kubt_get_insn(ar->pc + 1, 2);
		rn = __BITS(insn2, 12, 15);
		if (rn == ARM_FP_REGNUM || rn == ARM_PC_REGNUM)
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
		rn = __BITS(insn2, 8, 11);
		if (rn == ARM_FP_REGNUM || rn == ARM_PC_REGNUM)
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
	} else if (((insn & 0xffff) == 0xf85d) || ((insn & 0xffff) == 0xf8dd)) {
		/* ldr Rt,[sp,#+/-imm]{!} */
		/* ldr.w Rt,[sp,#+/-imm]{!}*/
		/* 0xf85d is also may be a tail of THUMB32 branch instruction */
		if (kubt_is_32bit_tail(ar->pc, start))
			return;
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		/* ldr.w pc, [sp], #imm */
		if (__BITS(insn2, 12, 15) == ARM_PC_REGNUM) {
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
			return;
		}

		if (__BITS(insn2, 12, 15) == ARM_LR_REGNUM) {
			ar->lr = kubt_get_user(ar->sp, 4);
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
			return;
		}

		/* Pre-indexed: index==TRUE, wback==TRUE */
		if (!(__BIT(insn2, 8) && __BIT(insn2, 10)))
			return;

		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;

		if (__BIT(insn2, 9))
			ar->sp -= __BITS(insn2, 0, 7);
		else
			ar->sp += __BITS(insn2, 0, 7);
	} else if ((insn & 0xfbff) == 0xf1ad) {
		/* sub.w sp, Rn, #imm */
		int imm;

		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if (__BITS(insn2, 8, 11) != ARM_SP_REGNUM)
			return;
		imm = ((__BITS(insn, 10, 10) << 11) |
			(__BITS(insn2, 12, 14) << 8) |
			__BITS(insn2, 0, 7));
		/* support sub.w sp, sp, #imm  only */
		ar->sp += kubt_thumb_expand_imm(imm);
	} else if ((insn & 0xfbff) == 0xf10d) {
		/* add.w sp, Rn, #imm */
		int imm;

		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		if (__BITS(insn2, 8, 11) != ARM_SP_REGNUM)
			return;
		imm = ((__BITS(insn, 10, 10) << 11) |
			(__BITS(insn2, 12, 14) << 8) |
			__BITS(insn2, 0, 7));

		/* support add.w sp, sp, #imm  only */
		ar->sp -= kubt_thumb_expand_imm(imm);
	/*** THUMB32 NEON instructions ***/
	} else if ((insn & 0xffff) == 0xed2d) {
		/* vpush { rlist } */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		insn2 = __BITS(insn2, 0, 7) >> 1;
		ar->sp += 8 * insn2;
	} else if ((insn & 0xffff) == 0xecbd) {
		/* vpop { rlist } */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		insn2 = __BITS(insn2, 0, 7) >> 1;
		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;
		ar->sp -= 8 * insn2;
	} else if ((insn & 0xfe2f) == 0xec2d) {
		/* vstm{ia,db} sp!, { rlist } */
		/* pc - IS_THUMB_ADDR(pc) + 2 */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		insn2 = __BITS(insn2, 0, 7);
		ar->sp += 8 * insn2;
	} else if ((insn & 0xfe3f) == 0xec3d) {
		/* vldm{ia,db} sp!, { rlist } */
		insn2 = kubt_get_insn(ar->pc + 1, 2);
		insn2 = __BITS(insn2, 0, 7);
		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;
		ar->sp -= 8 * insn2;
	}
}

/* handle ARM mode instructions */
static inline void kubt_handle_arm_insn(int *flags,
		struct kdbgd_bt_regs *ar, unsigned int insn, unsigned long start)
{
	if ((insn & 0xfffff000) == 0xe24dd000) {
		/* sub sp,sp, size */
		unsigned long imm = insn & 0xff;
		unsigned long rot = (insn & 0xf00) >> 7;

		imm = (imm >> rot) | (imm << (32 - rot));
		ar->sp += imm;
	} else if ((insn & 0xffff0000) == 0xe92d0000) {
		/*  stmfd sp!, {..., fp, ip, lr, pc}
		 * or
		 *  stmfd sp!, {a1, a2, a3, a4}
		 * or
		 *  push {...fp,ip,lr,pc} */
		int mask = insn & 0xffff;
		int rn = 0;

		/* Calculate offsets of saved registers.  */
		for (; rn <= ARM_PC_REGNUM; rn++) {
			if (!(mask & (1 << rn)))
				continue;
			/* lr is pushed on stack so read lr */
			if (rn == ARM_LR_REGNUM)
				ar->lr = kubt_get_user(ar->sp, 4);
			ar->sp += 4;
		}
	} else if ((insn & 0xffff0000) == 0xe58d0000) {
		/* str Rt, [sp, #imm] */
		unsigned int imm = insn & 0xff;

		if (__BITS(insn, 12, 15) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(ar->sp + imm, 4);
	} else if ((insn & 0xffff0000) == 0xe52d0000) {
		/* push or str, using sp */
		/* Pre-indexed: index==TRUE, wback==TRUE already covered */
		if (__BITS(insn, 12, 15) == ARM_LR_REGNUM)
			ar->lr = kubt_get_user(ar->sp, 4);
		ar->sp += __BITS(insn, 0, 11);
	} else if ((insn & 0x0ffffff0) == 0x012fff10) {
		/* bx<c><q> */
		ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
	} else if ((insn & 0x0ffffff0) == 0x01a0f000) {
		/* mov pc */
		ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
	} else if ((insn & 0xf12d00f0) == 0xe12d00f0) {
		/* strd Rt, [sp, #imm]! */
		unsigned long imm = __BITS(insn, 8, 11) << 4;
		imm |= insn & 0xf;
		ar->sp += imm;
	} else if ((insn & 0xffeff000) == 0xe28dd000) {
		/* add Rd, Rn, #n */
		unsigned long imm = insn & 0xff;
		unsigned long rot = (insn & 0xf00) >> 7;
		imm = (imm >> rot) | (imm << (32 - rot));
		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;
		ar->sp -= imm;
	} else if ((insn & 0xffff0fff) == 0xe49d0004) {
		/* single pop */
		if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
			return;

		if (__BITS(insn, 12, 15) == ARM_PC_REGNUM)
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
		else
			ar->sp -= 4;
	} else if ((insn & 0xff1f0000) == 0xe81d0000) {
		/*  pop { rlist }
		 * or
		 *  ldm{da,db,ia,ib} { rlist } {!} */
		int mask = insn & 0xffff;
		int rn = 0;

		if ((mask & (1 << ARM_PC_REGNUM)) ||
				(mask & (1 << ARM_FP_REGNUM))) {
			ar->pc = kubt_jumpover_pc(flags, ar->pc, start);
		} else {
			if (*flags & KUBT_FLAG_FUNC_EPILOGUE)
				return;

			for (; rn <= ARM_PC_REGNUM; rn++) {
				if (mask & (1 << rn))
					ar->sp -= 4;
			}
		}
	} else if ((insn & 0xefbf0f00) == 0xed2d0b00) {
		/* vpush {rlist} */
		ar->sp += 8 * __BITS(insn, 0, 7) >> 1;
	} else if ((insn & 0x0cbd0b00) == 0x0cbd0b00) {
		/* vpop {rlist} */
		*flags |= KUBT_FLAG_FUNC_EPILOGUE;
		if (*flags)
			return;
		ar->sp -= 8 * __BITS(insn, 0, 7) >> 1;
	} else if ((insn & 0xec3d0f00) == 0xec2d0b00) {
		/* vstm{ia,db} sp!, { rlist } */
		/* we probably should care about BIT:23 */
		ar->sp += 8 * __BITS(insn, 0, 7);
	} else if ((insn & 0xec3f0f00) == 0xec3d0b00 ||
			(insn & 0xec3f0f00) == 0x8c3d0b00) {
		/* vldm{ia,db} sp!, { rlist }*/
		*flags |= KUBT_FLAG_FUNC_EPILOGUE;
		if (*flags)
			return;
		/* we probably should care about BIT:23 */
		ar->sp -= 8 * __BITS(insn, 0, 7);
	}
}

int kubt_kdbgd_show_trace(struct mm_struct *tsk_mm, struct kdbgd_bt_regs *regs)
{
	struct kdbgd_bt_regs *ar = regs;
	unsigned int insn = 0;
	unsigned long start = 0, last_sp = 0;
	int ret, th_mode;
	int flags = 0, limit = 64;

	call_depth = 0;

	pr_info(KUBT_TRACE_HEADER, (unsigned int)ar->fp, (unsigned int)ar->pc,
			(unsigned int)ar->lr, (unsigned int)ar->sp,
			(unsigned int)regs->sp_end);

	ret = kubt_lookup_sym(tsk_mm, ar->pc, &start);
	if (ret != 0)
		return ret;

	/* THUMB mode addresses usually have 0 bit set. however, in tottally
	 * messed up cases
	 *
	 *	FP: 0x0, PC: 0xb6174338, RA: 0xc159
	 *	SP: 0xbdec28b0, Stack End: 0xbdec2ac0
	 *
	 * (yes, with 0x00 frame pointer) process may be killed while executing
	 * in THUMB function, but with clear 0 bit
	 *	PC: 1011 0110 0001 0111 0100 0011 0011 1000
	 *
	 * fix such addresses. for the first frame we decode we can rely on
	 * CPSR register -- THUMB mode `cpsr' reg sets PSR_T_BIT bit. */
#if 0
	if (thumb_mode(&regs->arch))
		ar->pc = MAKE_THUMB_ADDR(ar->pc);
#endif

	/* this case comes for built in pcesses. e.g.
	 * __aeabi_read_tp and [vdso] */
	if (ar->pc > 0xffff0000)
		return kubt_lookup_sym(tsk_mm, ar->lr, &start);

	/*ar->lr = 0;*/
	while (1) {
		th_mode = IS_THUMB_ADDR(ar->pc);
		/* THUMB addresses have 0 bit set, which is identical to
		 * `pc + 1'. In order to read correct `pc' we need to adjust
		 * pc address. */
		/* we read 4 bytes for ARM mode and 2 bytes for THUMB.
		 * Take special care of THUMB16/THUMB32 mode instructions */
		if (th_mode)
			insn = kubt_get_user(ar->pc -
					IS_THUMB_ADDR(ar->pc), 2);
		else
			insn = kubt_get_user(ar->pc, 4);

		if (th_mode) {
			kubt_handle_thumb_insn(&flags, ar, insn, start);
			ar->pc -= 2;
		} else {
			kubt_handle_arm_insn(&flags, ar, insn, start);
			ar->pc -= 4;
		}

		/* we might have undefined instructions. if we know start (IOW
		 * we know when to stop), then we can just skip anything that
		 * is not an ARM instruction. Example:
		 * 0x000151e0 <+3004>:  ; <UNDEFINED> instruction: 0xffff451c
		 * 0x000151e4 <+3008>:  ; <UNDEFINED> instruction: 0xffff4508
		 * 0x000151e8 <+3012>:  ; <UNDEFINED> instruction: 0xffff45bc
		 * 0x000151ec <+3016>:  andeq   r0, r0, r0, asr #6
		 * 0x000151f0 <+3020>:  andeq   r0, r0, r4, asr #6 */
		if (insn == INSN_ERR) {
			if (start)
				continue;
			return 0;
		}

		if (!ar->pc)
			return 0;

		if (ar->pc < start) {
			if (ar->lr == INSN_ERR)
				return 0;

			ar->pc = ar->lr;
			/* KUBT_FLAG_FUNC_EPILOGUE means that we hit function
			 * epilogue, but either:
			 * a) no start address for current function was found
			 * b) start and current pc addresses are located very
			 * close
			 *
			 * in that case we mark execution as pc_slow_path and
			 * continue with regular instruction decoding, but
			 * avoid stack unrolling instructions,
			 * like `add sp, #imm' */
			flags &= ~KUBT_FLAG_FUNC_EPILOGUE;
			/* check the caller side */
			ret = kubt_lookup_sym(tsk_mm, ar->pc, &start);
			if (ret != 0)
				return ret;
			if (last_sp >= ar->sp) {
				pr_err("KUBT: abort backtracing at SP: %x\n",
						(unsigned int)ar->sp);
				return 0;
			}
			last_sp = ar->sp;
			/* move away from calling instruction */
			ar->pc -= 2;
			if (!IS_THUMB_ADDR(ar->pc))
				ar->pc -= 2;

			if (ar->pc <= tsk_mm->start_code)
				return 0;
			limit--;
			if (unlikely(limit < 1)) {
				pr_err("KUBT: abort backtracing at SP: %x\n",
						(unsigned int)ar->sp);
				return 0;
			}
		}
	}
	return 0;
}
