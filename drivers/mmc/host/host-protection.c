#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>

#include "../../block/partitions/check.h"

#define PROTECT_RO  0
#define PROTECT_RW  1

struct write_protection {
    unsigned int part_addr;
    unsigned int part_size;
    unsigned int part_type;
};

int partition_cnt = 0;			/* partition count */
struct write_protection *wp = NULL;	/* alloc part_info as much as partition count */
int flash_protect = 0;			/* 0 : Not allowed, 1 : Allow write */

extern int micom_reboot( void );

static void violate_protection_area(unsigned int addr)
{
#ifndef CONFIG_VD_RELEASE
        panic("WRITE PROHIBITED : 0x%08x \n", addr);
#else
        printk("WRITE PROHIBITED : 0x%08x \n", addr);
	micom_reboot();
#endif

}

unsigned int check_protection_area(unsigned int address, struct mmc_host *host)
{
    int i = 0;
    if(flash_protect == 1)
	return PROTECT_RW; /* Allow write by force */
    else
    {
	if (!mmc_card_blockaddr(host->card))
		address >>= 9;
	for(i = partition_cnt ; i > 0 ; i--)
	{
	    if( address >= wp[i].part_addr )
	    {
		if(wp[i].part_type == PROTECT_RO)
		{
		    violate_protection_area(address);
		    return PROTECT_RO;
		}
		else
		    return wp[i].part_type;
	    }
	}
    }
    violate_protection_area(address);
    return PROTECT_RO;
}
EXPORT_SYMBOL(check_protection_area);

int init_write_protect(struct parsed_partitions *state)
{
    int p;
    if(state == NULL)
	return 0;

    if(wp == NULL) {
        wp = kmalloc(state->limit*sizeof(struct write_protection), GFP_KERNEL);
	 memset(wp, 0x0, state->limit*sizeof(struct write_protection));
    } else
	return 0;

    for(p = 1; p < state->limit; p++) {
	if(state->parts[p].from == 0)
	    break;

	wp[p].part_addr = (unsigned int)(state->parts[p].from);
	wp[p].part_size = (unsigned int)(state->parts[p].size);
	wp[p].part_type = PROTECT_RO;
#ifndef CONFIG_VD_RELEASE
	printk("[%d] %08x \n", p, wp[p].part_addr);
#endif
    }
    partition_cnt = p-1;
    return 0;
}
EXPORT_SYMBOL(init_write_protect);

void set_write_protect_rw(int part_num)
{
    if (wp == NULL)
	return;

    wp[part_num].part_type = PROTECT_RW;
}
EXPORT_SYMBOL(set_write_protect_rw);

void set_write_protect_ro(int part_num)
{
    if (wp == NULL)
	return;

    wp[part_num].part_type = PROTECT_RO;
}
EXPORT_SYMBOL(set_write_protect_ro);

int get_partition_cnt(void)
{
    return partition_cnt;
}
EXPORT_SYMBOL(get_partition_cnt);

