#ifndef HWCLOCK_REGS_H
#define HWCLOCK_REGS_H

#if defined(CONFIG_ARCH_SDP1304)     /* GolfP */
#define HW_CLOCK_PA   0x10F700C8
#define HW_CLOCK_FREQ 24000          /* KHz   */
#elif defined(CONFIG_ARCH_SDP1404)   /* HawkP */
#define HW_CLOCK_PA   0x10F900C0
#define HW_CLOCK_FREQ 24576          /* KHz   */
#elif defined(CONFIG_ARCH_SDP1406)   /* HawkM */
#define HW_CLOCK_PA   0x007900C0
#define HW_CLOCK_FREQ 24576          /* KHz   */
#else
#error Unknown arch
#endif

#if defined(CONFIG_ARCH_SDP1406)     /* HawkM */
#define SDP_PHYS_TO_VIRT(x) ((((x) & 0x00FFFFFF) | 0xFE000000) - 0x00100000)
#else
#define SDP_PHYS_TO_VIRT(x) (((x) & 0x00FFFFFF) | 0xFE000000)
#endif

#define HW_CLOCK_VA SDP_PHYS_TO_VIRT(HW_CLOCK_PA)

#endif /* HWCLOCK_REGS_H */
