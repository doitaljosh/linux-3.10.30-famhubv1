#include <mach/sdp_hwclock.h>

/* See explanation of those veraibles in the header above */

uint64_t __hack_ns;
uint64_t __delta_ns;

EXPORT_SYMBOL(__hack_ns);
EXPORT_SYMBOL(__delta_ns);
