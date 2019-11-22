#include_next <linux/math64.h>

#ifndef KDBUS_MATH64_H
#define KDBUS_MATH64_H

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0)

#include <linux/types.h>
#include <asm/div64.h>

#if BITS_PER_LONG == 64

/**
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 */
static inline u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

#elif BITS_PER_LONG == 32

/**
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 * @dividend:   64bit dividend
 * @divisor:    64bit divisor
 * @remainder:  64bit remainder
 *
 * This implementation is a comparable to algorithm used by div64_u64.
 * But this operation, which includes math for calculating the remainder,
 * is kept distinct to avoid slowing down the div64_u64 operation on 32bit
 * systems.
 */
#ifndef div64_u64_rem
extern u64 div64_u64_rem(u64 dividend, u64 divisor, u64 *remainder);
#endif

#endif

#endif

#endif /* KDBUS_MATH64_H */
