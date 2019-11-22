/*
 * include/linux/mincore.h
 *
 * This header file provides definitions & declarations for functions
 * and data used in fs/minimal_core.c for creating a corefile with
 * minimal info
 *       --> thread of crashing process
 *       --> note sections (NT_PRPSINFO, NT_SIGINFO, NT_AUXV, NT_FILE)
 *       --> crashing thread stack
 * The corefile created is compressed & encrypted
 * it needs a modified GDB which is shared with HQ
 *
 * Copyright 2014: Samsung Electronics Co, Pvt Ltd,
 * Author : Manoharan Vijaya Raghavan (r.manoharan@samsung.com).
 */

#ifndef _MINCORE_H
#define _MINCORE_H

#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/elf.h>

#include <linux/zlib.h>
#include <linux/zutil.h>
#include <linux/crc32.h>
#include <linux/crypto.h>
#include "../../init/secureboot/include/SBKN_rsa.h"
#include "../../init/secureboot/include/SBKN_bignum.h"
#include <linux/sort.h>

#ifdef CONFIG_SECURITY_SMACK
#define MINCORE_SMACK_LABEL "sys-assert::core"
#endif

struct minimal_coredump_params {
	siginfo_t *siginfo;
	struct pt_regs *regs;
	struct file *file;
#ifdef __KERNEL__
	int minimal_core_state;
	unsigned long crc;
	/* shift register contents */
	unsigned char *comp_buf;
	unsigned int total_size;
	unsigned long seed_crc;
	z_stream def_strm;
#define AES_BLK_SIZE 16
#define AES_KEY_LEN 16
#define RSA_ENC_AES_KEY_SIZE 128
#define AES_ENC_BUF_SIZE (16 * 1024)

#define ENCODE_IN_SIZE 96
#define ENCODE_OUT_SIZE 128
#define RSA_KEYBYTE_SIZE 128
	int bufs_index;
	char buf_encode_out[ENCODE_OUT_SIZE];
	char buf_encode_in[ENCODE_IN_SIZE];
	char buf_encode_in_aes[16];
	SDRM_BIG_NUM *mod;
	SDRM_BIG_NUM *exp;
	SDRM_BIG_NUM *base;
	SDRM_BIG_NUM *out;
	cc_u8 *pbbuf_mod;
	cc_u8 *pbbuf_exp;
	cc_u8 *pbbuf_base;
	cc_u8 *pbbuf_out;
	cc_u32 rsa_keybytelen;
	struct crypto_cipher *cip;
	char *aes_enc_buf;
	cc_u8 aes_key[AES_KEY_LEN];
	int rsa_enc_aes_key;
#endif
};

/* 4294967295, HARDCODED, but will avoid an unncessary kmalloc
* will work as long as ulong is 32 bits and it is so in 64 bit too */
#define ULONG_LEN 10

/* An ELF note in memory */
struct memelfnote {
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};


#define ARM_NREGS 14
#define LAST_WRITE 1

/* Too huge, but this is only for debugging, DO NOT ENABLE in production */
/* Enable for page level debugging */
/* #define MINIMAL_CORE_DEBUG_DETAIL 1 */
/* Enable this to debug all deflate blocks */
/* #define MINIMAL_CORE_DEBUG_DEFLATEBLOCK 1 */

/* refer to include/linux/zlib.h notes for zlib_deflate() comments for
   * explanation regarding 12 bytes
   */
#define MINIMAL_CORE_COMP_BUFSIZE (2 * PAGE_SIZE + 12)
#define MINIMAL_CORE_INIT_STATE 0
#define MINIMAL_CORE_COMP_STATE 1
#define MINIMAL_CORE_FINI_STATE 2

/* Written based on gzip-1.6 algorithm.doc */
struct gzip_header {
	unsigned char id[2];
	unsigned char cm;
	unsigned char flag;
	unsigned char mtime[4];
	unsigned char xfl;
	unsigned char os;
};

/* from gzip-1.6 util.c */
extern unsigned long updcrc(unsigned long *seed_crc, unsigned char const *s,
				unsigned long n);

#ifdef CORE_DUMP_USE_REGSET

struct elf_thread_core_info {
	struct elf_thread_core_info *next;
	struct task_struct *task;
	struct elf_prstatus prstatus;
	struct memelfnote notes[0];
};

struct elf_note_info {
	struct elf_thread_core_info *thread;
	struct memelfnote psinfo;
	struct memelfnote signote;
	struct memelfnote auxv;
	struct memelfnote files;
	siginfo_t csigdata;
	size_t size;
	int thread_notes;
};

#else

#ifndef user_siginfo_t
#define user_siginfo_t siginfo_t
#endif

struct elf_note_info {
	struct memelfnote *notes;
	struct elf_prstatus *prstatus;  /* NT_PRSTATUS */
	struct elf_prpsinfo *psinfo;    /* NT_PRPSINFO */
	struct list_head thread_list;
	elf_fpregset_t *fpu;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t *xfpu;
#endif
	user_siginfo_t csigdata;
	int thread_status_size;
	int numnote;
};

/* Here is the structure in which status of each thread is captured. */
struct elf_thread_status {
	struct list_head list;
	struct elf_prstatus prstatus;   /* NT_PRSTATUS */
	elf_fpregset_t fpu;     /* NT_PRFPREG */
	struct task_struct *thread;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t xfpu;       /* ELF_CORE_XFPREG_TYPE */
#endif
	struct memelfnote notes[3];
	int num_notes;
};

#endif /* CORE_DUMP_USE_REGSET */

extern int minmal_core_fill_note(struct elfhdr *elf, int phdrs,
		struct elf_note_info *info,
		siginfo_t *siginfo, struct pt_regs *regs);
extern void minimal_core_fill_phdr(struct elf_phdr *phdr, int sz,
		loff_t offset);
extern void minimal_core_free_note(struct elf_note_info *info);

struct svma_struct {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_flags;
	unsigned long vm_pgoff;
	struct file *vm_file;
	unsigned long dumped;
	struct list_head node;
};

struct addrvalue {
	unsigned long start;
	unsigned long end;
	/* To avoid to many loops
	 * this flag will be set to 0
	 * if an addr range is merged with other VMA
	 *                OR
	 * if an addr range is made invalid
	 */
	unsigned int dumped;
};

#define MINCORE_NAME ".mcore"
/* NOTE : MININMAL_CORE_LOCATION should have
* basedir and filedir, the last level is called
* as filedir and it will be the only thing mincore
* will attempt to create if not present
*/
#ifndef CONFIG_PLAT_TIZEN
#define MINIMAL_CORE_LOCATION "/mtd_rwcommon/error_log/"
#else
#define MINIMAL_CORE_LOCATION "/opt/usr/share/crash/core/mincore/"
#endif

extern char target_version[];

#define MINIMAL_CORE_TORETAIN 10UL
struct mcore_name_list {
	char *name;
	struct list_head list;
};

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
#define NT_SABSP_FILE_INFO	(int)(0xdeadbeaf)
#define pr_rlimit(fmt, ...)	\
	pr_emerg("Mincore_rlimit_nofile: "fmt, ##__VA_ARGS__)
extern struct mutex file_rlimit_mutex;
extern struct list_head file_rlimit_list;
void file_rlimit_dump(void);

struct open_file_info {
	struct task_struct  *overflow_tsk;
	int fd;
	fmode_t	f_mode;
	pid_t owner_pid;
	loff_t f_pos;
	const char *owner_name;
	const char *name;
	struct list_head node;
	struct task_struct *tsk;
};
#endif /* CONFIG_MINCORE_RLIMIT_NOFILE */

#endif /* _MINCORE_H */
