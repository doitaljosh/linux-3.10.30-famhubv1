/*
 * kdml_meminfo.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef __KDML_MEMINFO_H__

/* function to display kernel memory usage information */
void kdml_ctr_kernel_meminfo_on_off(void);

/* function to display user memory usage information */
void kdml_ctr_user_meminfo_on_off(void);

/* function to display memory usage summary */
void kdml_ctr_meminfo_summary_on_off(void);

/* Register function for kernel and user memory usage */
int kdml_ctr_register_meminfo_functions(void);

/* Unregister function for kernel and user memory usage */
void kdml_ctr_unregister_meminfo_functions(void);

/* turn off the messages of meminfo */
void kdml_ctr_meminfo_turnoff_messages(void);
#endif /* __KDML_MEMINFO_H__ */
