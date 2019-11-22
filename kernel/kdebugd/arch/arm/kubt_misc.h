/*
 * This is part of KUBT (kernel user backtrace).
 *
 * Sergey SENOZHATSKY, sergey1985.s@samsung.com
 */
#ifndef _KUBT_MISC_H_
#define _KUBT_MISC_H_

#define KUBT_TRACE_HEADER	"FP:0x%08x, PC:0x%08x, RA:0x%08x,"\
				"SP:0x%08x, Stack end:0x%08x\n"
#define __SUBMASK(x)	((1L << ((x) + 1)) - 1)
#define KUBT_FLAG_FUNC_EPILOGUE	(1 << 2)
#define __BIT(obj, st)	(((obj) >> (st)) & 1)
#define __BITS(obj, st, fn)	(((obj) >> (st)) & __SUBMASK((fn) - (st)))

#define KDEBUGD_PRINT_ELF   "#%d  0x%08lx in %s () from %s\n"
#define KDEBUGD_PRINT_DWARF "#%d  0x%08lx in %s () at %s:%d\n"

struct kubt_kdbgd_sym {
	int is_valid;
	char *sym_name;
	char *lib_name;
	unsigned long start;
#ifdef CONFIG_DWARF_MODULE
	struct aop_df_info *pdf_info;
#endif
};

#endif /*_KUBT_MISC_H_ */
