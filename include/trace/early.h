/*
 *  early.h
 *
 *  Copyright (C) 2014  Roman Pen
 *
 *  API for naive and simple but still working implementation of the early
 *  tracing which accepts messages from the beginning of the execution of
 *  the linux kernel (start_kernel routine) and then dumps everything from
 *  a lockless buffer to the original ftrace ring buffer.
 */
#ifndef _EARLY_H
#define _EARLY_H

#ifdef CONFIG_EARLY_TRACING
extern void trace_early_message(const char *str);
#else
static inline void trace_early_message(const char *str) {}
#endif

#endif /* _EARLY_H */
