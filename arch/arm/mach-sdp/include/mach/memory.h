/****************************************************************8
 *
 * arch/arm/mach-sdp/include/mach/memory.h
 * 
 * Copyright (C) 2010 Samsung Electronics co.
 * Author : tukho.kim@samsung.com
 *
 */
#ifndef _MACH_MEMORY_H_
#define _MACH_MEMORY_H_

#if defined(CONFIG_MACH_FOXAP)
#   include <mach/foxap/memory-foxap.h>
#elif defined(CONFIG_MACH_FOXB)
#   include <mach/foxb/memory-foxb.h>
#elif defined(CONFIG_MACH_GOLFS)
#   include <mach/golfs/memory-golfs.h>
#elif defined(CONFIG_MACH_GOLFP)
#   include <mach/golfp/memory-golfp.h>
#elif defined(CONFIG_MACH_ECHOP)
#   include <mach/echop/memory-echop.h>
#elif defined(CONFIG_OF) && defined(CONFIG_SPARSEMEM)
/* override phys/virt conversion macros */

#define MAX_PHYSMEM_BITS		32
#define SECTION_SIZE_BITS		23

#ifndef __ASSEMBLY__

extern phys_addr_t sdp_sys_mem0_size;
extern phys_addr_t sdp_sys_mem1_size;
extern phys_addr_t sdp_sys_mem0_base;
extern phys_addr_t sdp_sys_mem1_base;
extern phys_addr_t sdp_sys_mem2_base;

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
extern unsigned long	__pv_offset;
extern unsigned long	__pv_phys_offset;
#define PHYS_OFFSET	__pv_phys_offset
#else
#define PHYS_OFFSET	(sdp_sys_mem0_base)
#endif

#define __virt_to_phys(x) ({	\
	phys_addr_t ret = (phys_addr_t)x - PAGE_OFFSET;			\
	if (likely(ret < sdp_sys_mem0_size))				\
		ret += PHYS_OFFSET;					\
	else if (likely(ret < (sdp_sys_mem0_size + sdp_sys_mem1_size))) \
		ret = sdp_sys_mem1_base + (ret - sdp_sys_mem0_size);	\
	else								\
		ret = sdp_sys_mem2_base + (ret - sdp_sys_mem0_size - sdp_sys_mem1_size);	\
	ret;	\
	})

#define __phys_to_virt(x) ({	\
	unsigned long ret;	\
	if (likely((x - PHYS_OFFSET) < sdp_sys_mem0_size)) 	\
		ret = PAGE_OFFSET + (x - PHYS_OFFSET);		\
	else if (likely((x - sdp_sys_mem1_base) < sdp_sys_mem1_size))	\
		ret = PAGE_OFFSET + (unsigned long)sdp_sys_mem0_size + 	\
			(unsigned long)(x - sdp_sys_mem1_base);		\
	else	\
		ret = PAGE_OFFSET + (unsigned long)(sdp_sys_mem0_size + sdp_sys_mem1_size) + \
				(unsigned long)(x - sdp_sys_mem2_base);	\
	ret;	\
	})
#endif	/* __ASSEMBLY__ */

#else
#define PLAT_PHYS_OFFSET		UL(0x40000000)
#endif 

#endif

