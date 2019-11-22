/*
 *  Hex dump a memory range for debugging
 * 
 *  Copyright (C) 2013  Ajeet Yadav
 * 
 *  This code is released using a dual license strategy: BSD/GPL
 *  You can choose the licence that better fits your requirements.
 * 
 *  Released under the terms of 3-clause BSD License
 *  Released under the terms of GNU General Public License Version 2.0
 * 
 */ 
#include <linux/kernel.h>

void dump_mem_kernel(const char *str, unsigned long bottom, unsigned long top);

void dump_mem_kernel(const char *str, unsigned long bottom, unsigned long top)
{
        unsigned long p;
        int i;

        printk(KERN_CONT "%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

        for (p = bottom & ~31; p <= top;) {
                printk(KERN_CONT "%04lx: ", p & 0xffff);
                for (i = 0; i < 8; i++, p += 4) {
                        if (p < bottom || p > top)
                                printk(KERN_CONT "         ");
                        else
                                printk(KERN_CONT "%08lx ", *(unsigned long*)p);
                }
                printk(KERN_CONT "\n");
        }
}
