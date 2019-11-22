/*
 *  kernel/kdebugd/elf/kdbg_elf_sym_api.h
 *
 *  Symbol (ELF) public interface API's are defined here
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-11-02 Created by karuna.nithy@samsung.com.
 *
 */

#ifndef _LINUX_KDBG_ELF_SYM_API_H
#define _LINUX_KDBG_ELF_SYM_API_H

#include "kdbg_elf_dem_api.h"
#include "kdbg_util.h"

#define KDBG_ELF_SYM_NAME_LENGTH_MAX 	1024

#define KDBG_ELF_SYM_MAX_SO_LIBS  		128

#define KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN 128

/* Max no of files to parsed. This includes the current(.) and parent directory(..). */
#define KDBG_ELF_MAX_FILES 				256

/* Max length of ELF path */
#define KDBG_ELF_MAX_PATH_LEN 			512

/* Max elf name length */
#define KDBG_ELF_MAX_ELF_FILE_NAME_LEN	128

/* Max num of USB supported */
#define KDBG_ELF_MAX_USB 	10

/*Max no of scan dir recurssion */
#define KDBG_ELF_MAX_PATHLIST_ENTRIES  32

/* This Structure Holds the Symbol related information */
typedef struct {
	unsigned int st_name;	/* Symbol name, index in string tbl */
	unsigned char st_info;	/* Type and binding attributes */
	unsigned int st_value;	/* address of the symbol */
	unsigned int st_size;	/* Associated symbol size */
} kdbg_elf_kernel_symbol_item;

/* This enum define the ELF Status */
typedef enum {
	KDBG_ELF_NO_SYMBOL = 0, /*ELF match but Stripped i.e having no symbols */
	KDBG_ELF_MISMATCH, /* ELF Mismatch found old ELF vs newly compiled ELF*/
	KDBG_ELF_MATCH, /* ELF Match and having symbols*/
	KDBG_ELF_SYMBOLS/* ELF having symbols*/
} kdbg_elf_status;

/*
 *   The structure temporary dirent which is used for storing
 *   the info  which retreive from sys_getdents64 it has been
 *   already defind in kernel using it by custmized
 *   The structure contains ELF reated information
 */
struct kdbg_elf_dir_list {
	int num_files;
	char path_dir[KDBG_ELF_MAX_PATH_LEN];
	struct ___dirent64 {
		u64 d_ino;
		s64 d_off;
		unsigned short d_reclen;
		unsigned char d_type;
		char d_name[KDBG_ELF_MAX_ELF_FILE_NAME_LEN];
	} dirent[KDBG_ELF_MAX_FILES];
};

/* The structure contains no fo files related information found in USB */
struct kdbg_elf_usb_path {
	char name[KDBG_ELF_MAX_USB][KDBG_ELF_MAX_PATH_LEN];
	int num_usb;
};

struct kdbg_elf_elf_file_st {
	char name[KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN];	/* exe or shared lib name */
	unsigned long start_addr;		/* vma start address */
	unsigned long end_addr;		/* vma end address */
	unsigned long inod_num;		/* i_nod number to compare similar elf file */
};

struct kdbg_elf_fs_lib_paths {
	struct kdbg_elf_elf_file_st elf_file[KDBG_ELF_SYM_MAX_SO_LIBS];	/* elf file struct */
	int num_paths;		/* no of elf files */
};


/* This Structure Holds the total no ELF files in USB */
typedef struct {
	struct list_head usb_elf_list;
	char elf_name_actual_path[KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN];	/* ELF names with path */
	char elf_name[KDBG_ELF_MAX_ELF_FILE_NAME_LEN];	/* ELF names detect from USB */
	kdbg_elf_kernel_symbol_item *kdbg_elf_sym_head;	/* Total no of ELF found in USB */
	int elf_symbol_count;	/* Total no of ELF Symbol Found in the directory */
	int type_sym_dym;	/* 0 = normal, 1 = dynamic */
#if 1				/* FULL_MEM */
	char *sym_buff;		/* Holds Symbol name */
#endif
	uint32_t act_sym_str_size; /* Hold the total sym buff size*/
	uint32_t sym_str_size;	/* Hold the total sym buff size */
	uint32_t sym_str_offset;	/* Hold the total sym buff size */
	char path_name[KDBG_ELF_MAX_PATH_LEN];	/* USB Path */
	uint32_t virtual_addr;	/* Virtual Addr */
	uint16_t file_type;	/* file Type */
#ifdef CONFIG_DWARF_MODULE
	u_int32_t dbg_info;
	uint32_t dbg_line_buf_size;	/* Namit:  Temporary no need */
	uint32_t dbg_line_buf_offset;	/* Namit:  Temporary no need */
	struct line_table_info *dbg_line_tables;	/* Namit: for debug line table parsing */
	char *gnu_debug_buf;
	unsigned long file_crc;
	unsigned long gnu_dbg_file_crc;
	void *gnu_link;
#endif				/* CONFIG_DWARF_MODULE */
	int elf_status; /* hold the ELF Status */
} kdbg_elf_usb_elf_list_item;



/* aop symbol load/unload notification function prototype */
typedef void (*kdbg_elf_symbol_load_notification) (int elf_load_flag);

/* The fucntion prototype for reading the file in the given directory */
int kdbg_elf_dir_read(struct kdbg_elf_dir_list *dir_list);

/*  The fucntion prototype for reading the file in the given directory  */
int kdbg_elf_usb_detect(struct kdbg_elf_usb_path *usb_path);

/* to register elf file load/unload notification function */
void
kdbg_elf_sym_register_oprofile_elf_load_notification_func
(kdbg_elf_symbol_load_notification func);
/* to unload ELF database */
void kdbg_elf_sym_delete(void);

/* Get the func name  after Search the symbol from given elf filename and addres */
int kdbg_elf_get_symbol(char *pfilename, unsigned int symbol_addr,
			unsigned int symbol_len,
			struct aop_symbol_info *symbol_info);

int kdbg_elf_get_all_pid_db_for_all_process(struct kdbg_elf_fs_lib_paths
					    *p_lib_paths);

/* get the symbol and lib using pid & PC value */
int kdbg_elf_get_symbol_and_lib_by_mm(struct mm_struct *tsk_mm, unsigned int sym_addr,
				       char *plib_name,
				       struct aop_symbol_info *symbol_info,
				       unsigned int *start_addr);
/*Give the ELF name without path name out of the full name*/
char *kdbg_elf_base_elf_name(const char *file);

/* get plist node if the elf is loaded */
kdbg_elf_usb_elf_list_item * lookup_elf_file_in_db(char *elf_file);

/* symbol name demangle function */
int kdbg_elf_sym_demangle(char *buff, char *new_buff, int buf_len);

/* symbol name demangle function */
char * kdbg_elf_sym_demangle_wrap(const char *buff);

/* to load executables and share libraries of the given PID. */

/* get the symbol using pid & PC value */
int kdbg_elf_get_symbol_by_pid(pid_t pid,
			       unsigned int sym_addr, char *sym_name);

/* Dump symbol of user stack with pid */
int kdbg_elf_show_symbol_of_user_stack_with_pid(void);

/* Load elf database by pid. pid can be a single pid or collection of pid.
First it will collect all elf file belongs to the process id and prepare the elf file list
which is belongs to the given (collection of) process id and load the elf database */
int kdbg_elf_load_elf_db_by_pids(const pid_t *ppid, int num_pids);

/* collect all the process id and collect elf files belongs to the process id
and load the elf databae */
int kdbg_elf_load_elf_db_for_all_process(void);

/* load elf database by elf file  */
kdbg_elf_usb_elf_list_item * kdbg_elf_load_elf_db_by_elf_file(char *elf_file);

/* load elf database by elf file  */
kdbg_elf_usb_elf_list_item * load_elf_db_by_elf_file(char *elf_file, int elf_load_from_usb, int elf_match);

/* Runtime Setting to Enable/ Disable ELF Module (Enable = 1, Disable = 0) */
extern int config_kdbg_elf_module_enable_disable;

/*
  * ELF Module init function, which initialize ELF Module and start functions
  * and allocate kdbg mem module (Hash table)
  */
int kdbg_elf_init(void);


int kdbg_elf_sym_find(unsigned int address, kdbg_elf_usb_elf_list_item *plist, char *pfunc_name,
		unsigned int symbol_len, unsigned int *start_addr);

#endif /* !_LINUX_KDBG_ELF_SYM_API_H */
