/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * debug_print.c
 */

#if defined(CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE) && defined(CONFIG_ARM)
#include <linux/io.h>
#include <asm/mach/map.h>
#endif
#include <linux/buffer_head.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include "../mount.h"

#include "debug_print.h"
#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"

#define	DATABLOCK	0
#define	METABLOCK	1
#define	VERSION_INFO	"Ver 3.0"

static char file_name_buf[PATH_MAX];
static const char *file_name;

static atomic_t	debug_print_once = ATOMIC_INIT(0);

#ifndef CONFIG_SEPARATE_PRINTK_FROM_USER
#define sep_printk_start
#define sep_printk_end
#else
extern void _sep_printk_start(void);
extern void _sep_printk_end(void);
#define sep_printk_start _sep_printk_start
#define sep_printk_end _sep_printk_end
#endif

/*
 * Dump out the contents of some memory nicely...
 */
static void dump_mem_be(const char *str, unsigned long bottom,
			unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	pr_err("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p <= top; i++, p += 4) {
			if (p >= bottom && p <= top) {
				unsigned long val;
				if (__get_user(val, (unsigned long *)p) == 0) {
					val = __cpu_to_be32(val);
					sprintf(str + i * 9, " %08lx", val);
				} else
					sprintf(str + i * 9, " ????????");
			}
		}
		pr_err("%04lx:%s\n", first & 0xffff, str);
	}

	set_fs(fs);
}

#ifdef CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE
static int sp_memcmp(const void *cs, const void *ct, size_t count, int *offset)
{
	const unsigned char *su1, *su2;
	int res = 0;

	pr_err("memcmp :  0x%p(0x%08x) <-> 0x%p(0x%08x)\n",
	       cs, *(unsigned int *)cs, ct, *(unsigned int *)ct);

	*offset = count;

	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--) {
		res = *su1 - *su2;
		if (res)
			break;
	}

	*offset = *offset - count;

	return res;
}

/* Verfication of flash data.
 * Called with disabled premption.
 */
static void __debug_auto_diagnose(struct debug_print_state *d,
				  int all_block_read,
				  int fail_block)
{
	struct squashfs_sb_info *msblk = d->sb->s_fs_info;
	char *block_buffer = NULL;
	char *nc_bh_bdata[32] = {0,};
	int diff_offset = 0;
	int detect_origin_vs_ncbh = 0;
	int detect_bh_vs_ncbh = 0;
	int check_unit = 0;
	int i = 0;

	pr_err("---------------------------------------------------------------------\n");
	pr_err("[verifying flash data]\n");

	if (all_block_read)
		check_unit = d->b;
	else
		check_unit = 1;

	/* 1. buffer allocation */
	block_buffer = kmalloc(msblk->devblksize * check_unit, GFP_KERNEL);

	if (!block_buffer) {
		pr_emerg("verifying flash failed - not enough memory\n");
		goto diagnose_buff_alloc_fail;
	}

	/* 2. copy original data */
	for (i = fail_block ; i < fail_block + check_unit; i++) {
		if (all_block_read)
			memcpy(block_buffer + i * msblk->devblksize,
			       d->bh[i]->b_data, msblk->devblksize);
		else
			memcpy(block_buffer, d->bh[i]->b_data,
			       msblk->devblksize);

		clear_buffer_uptodate(d->bh[i]);
	}

	preempt_enable();

	/* 3. Reread buffer block from flash */
	if (d->block_type == DATABLOCK)
		ll_rw_block(READ, check_unit, d->bh);
	else {
		/* 1st block of Metadata is used for special purpose */
		/* FIXME: WTF! who will care about freing of previous one? */
		d->bh[0] = d->get_block_length(d->sb, &d->__cur_index,
					       &d->__offset, &d->__length);
		if (!d->bh[0]) {
			pr_err("---------------------------------------------------------------------\n");
			pr_err("[ Diagnose fail - Metablock 0 loading trial is failed...            ]\n");
			pr_err("[                 Check Metablock 0 on squashfs image               ]\n");
			pr_err("---------------------------------------------------------------------\n");

			preempt_disable();
			goto diagnose_fail;
		}

		ll_rw_block(READ, d->b - 1, d->bh + 1);
	}

	/* 3-1. wait complete read */
	for (i = fail_block ; i < fail_block + check_unit; i++)
		wait_on_buffer(d->bh[i]);

	preempt_disable();

	/* 4. ioremap buffer_head to see the uncache area */
	for (i = fail_block ; i < fail_block + check_unit; i++) {
#if defined(CONFIG_ARM)
		nc_bh_bdata[i] = __arm_ioremap(virt_to_phys(d->bh[i]->b_data),
					       msblk->devblksize, MT_UNCACHED);
#elif defined(CONFIG_MIPS)
		nc_bh_bdata[i] = ioremap(virt_to_phys(d->bh[i]->b_data),
					 msblk->devblksize);
#endif
	}

	/* 5. Checking buffer : Original Data vs Reread Data (cached)
	   vs Reread Data (uncached) */
	for (i = fail_block ; i < fail_block + check_unit; i++) {
		detect_origin_vs_ncbh =
			sp_memcmp(block_buffer + (msblk->devblksize * i),
				  nc_bh_bdata[i], msblk->devblksize,
				  &diff_offset);

		detect_bh_vs_ncbh =
			sp_memcmp(d->bh[i]->b_data, nc_bh_bdata[i],
				  msblk->devblksize, &diff_offset);

		if (detect_origin_vs_ncbh || detect_bh_vs_ncbh) {
			diff_offset = diff_offset & ~(0x3);
			/* We found failed block */
			fail_block = i;
			goto diagnose_detect;
		}
		diff_offset = 0;
	}

	/* 6. print the result */
	pr_err("---------------------------------------------------------------------\n");
	pr_err("[ result - Auto diagnose can't find any cause.....;;;;;;;            ]\n");
	pr_err("[        - flash image is broken or update is canceled abnormally??? ]\n");
	pr_err("---------------------------------------------------------------------\n");

	/* goto the end */
	goto diagnose_end;

diagnose_detect:
	pr_err("bh[%2d]:0x%p | bh[%2d]->b_data:%p (physical addr)\n",
	       fail_block, d->bh[fail_block], fail_block,
	       nc_bh_bdata[fail_block]);

	pr_err("---------------------------------------------------------------------\n");
	dump_mem_be("DUMP Fail Block ( non cached data ) ",
		    (unsigned int)nc_bh_bdata[fail_block],
		    (unsigned int)nc_bh_bdata[fail_block] +
				  msblk->devblksize - 4); /* e.g. 4k */

	pr_err("---------------------------------------------------------------------\n");
	pr_err("[ verifying result - Original Data vs Reread Data (Uncached) : ");
	pr_err("%s", detect_origin_vs_ncbh ? "FAIL ]\n" : "PASS ]\n");
	pr_err("[                  - Data(cached) vs Data(Uncached)          : ");
	pr_err("%s", detect_bh_vs_ncbh ? "FAIL ]\n" : "PASS ]\n");
	pr_err("---------------------------------------------------------------------\n");
	pr_err("[ verifying result - Cache and Memory data is different.........!!  ]\n");
	pr_err("---------------------------------------------------------------------\n");

	pr_err("         Original   -- Cached Data -- Non cached Data\n");
	pr_err(" Addr :  0x%8p -- 0x%8p  -- 0x%8p\n",
	       block_buffer + diff_offset,
	       d->bh[fail_block]->b_data  + diff_offset,
	       nc_bh_bdata[fail_block] + diff_offset);
	pr_err(" Value:  0x%08x -- 0x%08x  -- 0x%08x  (big endian)\n",
	       __cpu_to_be32(*(unsigned int *)(block_buffer +
					      (fail_block * msblk->devblksize) +
					      diff_offset)),
	       __cpu_to_be32(*(unsigned int *)(d->bh[fail_block]->b_data +
					      diff_offset)),
	       __cpu_to_be32(*(unsigned int *)(nc_bh_bdata[fail_block] +
					      diff_offset)));

diagnose_end:
	/* 7. unmap mappings  */
	for (i = fail_block ; i < fail_block + check_unit; i++) {
		if (nc_bh_bdata[i])
			iounmap(nc_bh_bdata[i]);
	}

diagnose_fail:
	kfree(block_buffer);

diagnose_buff_alloc_fail:
	pr_err("=====================================================================\n");
}
#endif /* CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE */


/*
 * Save latest file name.
 * Here we do not care about anything ...
 * Yeah, that's it! This is the design!
 */
void debug_set_file_name(struct file *file)
{
	if (unlikely(!file))
		return;
	file_name = d_path(&file->f_path, file_name_buf, sizeof(file_name_buf));
	if (IS_ERR(file_name))
		file_name = "(error)";
}


/* Print everything we can */
void debug_print(struct debug_print_state *d)
{
	/* FIXME: currently we suppose that decompression failed,
	   thus read was fully completed */
	int all_block_read = 1;

	/* FIXME: this old_k and fail_block were always zero.
	   the author of the following lines did not understand the logic */
	int old_k = 0;
	int fail_block = 0;

	int i = 0;
	char bdev_name[BDEVNAME_SIZE];
	struct squashfs_sb_info *msblk = d->sb->s_fs_info;

	/* We do debug printing only once */
	if (atomic_inc_not_zero(&debug_print_once))
		return;

	preempt_disable();

	/* Start separation print from user */
	sep_printk_start();

	pr_err("---------------------------------------------------------------------\n");
	pr_err(" Current : %s(%d)\n", current->comm, task_pid_nr(current));
	pr_err("---------------------------------------------------------------------\n");
	pr_err("== [System Arch] Squashfs Debugger - %7s ======== Core : %2d ====\n",
	       VERSION_INFO, current_thread_info()->cpu);
	pr_err("---------------------------------------------------------------------\n");
	if (d->block_type == METABLOCK) {
		pr_err("[MetaData Block]\nBlock @ 0x%llx, %scompressed size %d, nr of b: %d\n",
		       d->index, d->compressed ? "" : "un", d->__length,
		       d->b - 1);
		pr_err("- Metablock 0 is broken.. compressed block - bh[0]\n");
	} else {
		pr_err("[DataBlock]\nBlock @ 0x%llx, %scompressed size %d, src size %d, nr of bh :%d\n",
		       d->index, d->compressed ? "" : "un", d->__length,
		       d->srclength, d->b);
		pr_err("- %s all compressed block (%d/%d)\n",
		       all_block_read ? "Read" : "Didn't read",
		       all_block_read ? d->b : old_k + 1, d->b);
	}

	pr_err("---------------------------------------------------------------------\n");
	if (d->next_index)
		pr_err("[Block: 0x%08llx(0x%08llx) ~ 0x%08llx(0x%08llx)]\n",
		       d->index >> msblk->devblksize_log2, d->index,
		       *d->next_index >> msblk->devblksize_log2,
		       *d->next_index);
	else
		pr_err("[Block: 0x%08llx(0x%08llx) ~ UNDEFINED]\n",
				d->index >> msblk->devblksize_log2, d->index);
	pr_err("\tlength : %d , device block_size : %d\n",
			d->__length, msblk->devblksize);
	pr_err("---------------------------------------------------------------------\n");

	if (all_block_read)
		pr_err("<< First Block Info >>\n");
	else
		pr_err("<< Fail Block Info >>\n");

	pr_err("- bh->b_blocknr : %4llu (0x%08llx x %4d byte = 0x%08llx)\n",
	       d->bh[fail_block]->b_blocknr,
	       d->bh[fail_block]->b_blocknr,
	       d->bh[fail_block]->b_size,
	       d->bh[fail_block]->b_blocknr * d->bh[fail_block]->b_size);
	pr_err("- bi_sector     : %4llu (0x%08llx x  512 byte = 0x%08llx)\n",
	       d->bh[fail_block]->b_blocknr * (d->bh[fail_block]->b_size >> 9),
	       d->bh[fail_block]->b_blocknr * (d->bh[fail_block]->b_size >> 9),
	       /* sector size = 512byte fixed */
	       d->bh[fail_block]->b_blocknr *
		       (d->bh[fail_block]->b_size >> 9) * 512);
	pr_err("- bh[%d]->b_data : 0x%p\n", fail_block,
	       d->bh[fail_block]->b_data);
	pr_err("---------------------------------------------------------------------\n");
	pr_err("Device : %s\n", bdevname(d->sb->s_bdev, bdev_name));
	pr_err("---------------------------------------------------------------------\n");
	print_mounts();
	pr_err("---------------------------------------------------------------------\n");

	if (d->block_type == METABLOCK)
		pr_err("MetaData Access Error : Maybe mount or ls problem..????\n");
	else {
		pr_err(" - CAUTION : Below is the information just for reference ....!!\n");
		pr_err(" - LAST ACCESS FILE : %s\n", file_name);
	}

	pr_err("---------------------------------------------------------------------\n");

	for (i = 0 ; i < d->b; i++) {
		pr_err("bh[%2d]:0x%p", i, d->bh[i]);
		if (d->bh[i]) {
			pr_err(" | bh[%2d]->b_data:0x%p | ", i,
			       d->bh[i]->b_data);
			pr_err("bh value :0x%08x",
			    __cpu_to_be32(*(unsigned int *)(d->bh[i]->b_data)));
			if (fail_block == i)
				pr_err("*");
		}
		pr_err("\n");
	}
	pr_err("---------------------------------------------------------------------\n");
	pr_err("[ Original Data Buffer ]\n");

	for (i = 0 ; i < d->b; i++) {
		pr_err("bh[%2d]:0x%p", i, d->bh[i]);
		if (d->bh[i]) {
			pr_err(" | bh[%2d]->b_data:0x%p | ",
			       i, d->bh[i]->b_data);
			dump_mem_be("DUMP BH->b_data",
				    (unsigned int)d->bh[i]->b_data,
				    (unsigned int)d->bh[i]->b_data +
						  msblk->devblksize - 4);
		}
		pr_err("\n");
	}

#ifdef CONFIG_SQUASHFS_DEBUGGER_AUTO_DIAGNOSE
	/* Do something probably very useful */
	__debug_auto_diagnose(d, all_block_read, fail_block);
#endif

	/* End separation */
	sep_printk_end();
	preempt_enable();
}
