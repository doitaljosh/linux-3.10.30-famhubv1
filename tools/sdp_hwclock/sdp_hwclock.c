#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>

#ifndef PR_TASK_PERF_USER_TRACE
#define PR_TASK_PERF_USER_TRACE 666
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE		(sysconf(_SC_PAGESIZE))
#define PAGE_MASK		(~(PAGE_SIZE-1))
#endif

#ifndef do_div
#define do_div(n,base) ({						\
			uint32_t __base = (base);			\
			uint32_t __rem;					\
			__rem = ((uint64_t)(n)) % __base;		\
			(n) = ((uint64_t)(n)) / __base;			\
			__rem;						\
		})
#endif

/* Should be the last, because it comes from kernel sources
   and we have to override some kernel defines */
#include "../../include/generated/autoconf.h"
#include "../../arch/arm/mach-sdp/include/mach/sdp_hwclock.h"

#ifndef CONFIG_SDP_HW_CLOCK
#error SDP HW clock is undefined
#endif

struct hwclk {
	void     *ptr;
	uint32_t *hwclk;
	int       fd;
};

static int hwclk_mmap(struct hwclk *hwclk)
{
	unsigned long hwclk_addr = hwclock_get_pa();

	/* Why O_SYNC? sdp_hwmem.c driver mmaps page for us as UNCACHED */
	hwclk->fd = open("/dev/sdp_hwmem", O_RDONLY | O_SYNC);
	if (hwclk->fd < 0) {
		perror("can't open mem device\n");
		return 0;
	}

	hwclk->ptr = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, hwclk->fd,
			  hwclk_addr & PAGE_MASK);
	if (hwclk->ptr == MAP_FAILED) {
		perror("mmap failed\n");
		close(hwclk->fd);
		return 0;
	}

	hwclk->hwclk = hwclk->ptr + (hwclk_addr & (PAGE_SIZE - 1));

	return 1;
}

static void hwclk_unmap(struct hwclk *hwclk)
{
	munmap(hwclk->ptr, 4096);
	close(hwclk->fd);
}

static uint64_t hwclk_ns(struct hwclk *hwclk)
{
	return hwclock_ns(hwclk->hwclk);
}

int main(int argc, char *argv[])
{
	int len, res;
	char msg[128];
	uint64_t hw_ns;
	struct hwclk hwclk;

	(void)argc; (void)argv;

	if (!hwclk_mmap(&hwclk))
		return -1;

	/* 1 clock checkpoint */
	hw_ns = hwclk_ns(&hwclk);
	len = snprintf(msg, sizeof(msg), "hwclock1: %03.04f",
				   hw_ns / 1000000000.0);
	res = prctl(PR_TASK_PERF_USER_TRACE, msg, len);
	assert(res == 0);
	printf("%s\n", msg);
	sleep(1);

	/* 2 clock checkpoint */
	hw_ns = hwclk_ns(&hwclk);
	len = snprintf(msg, sizeof(msg), "hwclock2: %03.04f",
				   hw_ns / 1000000000.0);
	res = prctl(PR_TASK_PERF_USER_TRACE, msg, len);
	assert(res == 0);
	printf("%s\n", msg);
	sleep(1);

	/* 3 clock checkpoint */
	hw_ns = hwclk_ns(&hwclk);
	len = snprintf(msg, sizeof(msg), "hwclock3: %03.04f",
				   hw_ns / 1000000000.0);
	res = prctl(PR_TASK_PERF_USER_TRACE, msg, len);
	assert(res == 0);
	printf("%s\n", msg);

	hwclk_unmap(&hwclk);

	return 0;
}
