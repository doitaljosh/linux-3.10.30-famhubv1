/*
 * dtvlogd.c
 *
 * Copyright (C) 2011, 2012 Samsung Electronics
 * Created by choi young-ho, kim gun-ho, mun kye-ryeon
 *
 * This used to save console message.
 *
 * NOTE:
 *     DTVLOGD               - Automatically store printk/printf messages
 *                             to kernel buffer whenever developer use printk/printf.
 *     USBLOG                - Periodically store DTVLOGD buffer to USB
 *                             Storage when given command using "echo <USB_PATH> > /proc/dtvlogd
 *     DTVLOGD_DEBUG         - Debugging feature of dtvlogd, adds debugging information
 *
 *     DTVLOGD_PROC_READ     - Read interface to print the kernel ringbuffer
 *     (cat /proc/dtvlogd)     the buffer is cleared after reading
 *
 *     DTVLOGD_ALL_PROC_READ - Read interface to print all the logged messages
 *     (cat /proc/dtvlogd_all)
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/fcntl.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/slab.h>

/*
 * We need to set DTVLOGD_BUFFER_VIRTUAL_ADDRESS in arch-specific file.
 * The address should be valid and used exclusively by dtvlogd.
 */
#include <asm/fixmap.h>
#include <linux/vmalloc.h>

/* Control Version */
#define DTVLOGD_VERSION	"6.0.4"

/* Control Preemption */
#define DTVLOGD_PREEMPT_COUNT_SAVE(cnt) \
	do {				\
		cnt = preempt_count();	\
		while (preempt_count())	\
			preempt_enable_no_resched(); \
	} while (0)

#define DTVLOGD_PREEMPT_COUNT_RESTORE(cnt) \
	do {				\
		int i;			\
		for (i = 0; i < cnt; i++)	\
			preempt_disable(); \
	} while (0)


#define USBLOG_SAVE_INTERVAL	5 /* Interval at which Log is saved to USB */

/* Buffer Size Config */
#define CONFIG_DLOG_BUF_SHIFT     18  /* 256 KB */

/* Dtvlogd main Log Buffer */
#define __DLOG_BUF_LEN	(1 << CONFIG_DLOG_BUF_SHIFT)
#define DLOG_BUF_MASK	(dlog_buf_len - 1)
#define DLOG_BUF(idx)	dlog_buf[(idx) & DLOG_BUF_MASK]
#define DLOG_INDEX(idx)	((idx) & DLOG_BUF_MASK)

#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
/*
 * Emergency Log Memory Map
 *
 * +--------------------------+ 0x0002 0000 (128KB)
 * |  Reserved Area           |
 * |   (63.9KB)               |
 * +--------------------------+ 0x0001 0010 (64KB + 16 Bytes)
 * | Mgmt Data (16 Bytes)     |
 * +--------------------------+ 0x0001 0000 (64KB)
 * | Emeg Log Buffer          |
 * |    (64KB)    (B)         |
 * +--------------------------+ 0x0000 0000 (0KB)    ( A )
 *
 * if emeg_log_len is kept as 128KB, then below distribution:
 *
 * (A) - This is the base address of emergnecy log.
 *       Base address is defined with emeg_log_addr.
 *       It is configured from kernel params
 *
 * (B) - This is the buffer size to save emergency log.
 *       dram_buf_len is defined for this, and taken as half of emeg_log_len.
 *
 */

/* TODO: change the limitation to use maximum available space,
 * subject to condition that the address is in power of 2.
 * e.g. if 130KB is provided, we can use:
 *      128 KB for emeg log buffer
 *      2 KB for Mgmt data
 */
static resource_size_t emeg_log_addr; /* should be aligned to dram_buf_len */
static int emeg_log_len;
static int dram_buf_len; /* emergency log buffer */
static int dram_buf_mask;
#define DRAM_INDEX(idx)	((idx) & dram_buf_mask)

#define RAM_DATA_ADDR(buf_addr)  (buf_addr + dram_buf_len)
#define RAM_START_ADDR(base)   (base + 0x4)
#define RAM_END_ADDR(base) (base + 0x8)
#define RAM_CHARS_ADDR(base)   (base + 0xC)

#define EMERGENCY_MAGIC_NUM (0xbabe)
#ifdef CONFIG_PLAT_TIZEN
static const char *RAM_FLUSH_FILE = "/var/log/emeg_log.txt";
#else
static const char *RAM_FLUSH_FILE = "/mtd_rwarea/emeg_log.txt";
#endif

void __iomem *ram_char_buf;
void __iomem *ram_info_buf;
static int ramwrite_start;
static int ram_clear_flush;
static unsigned int ram_log_magic;

static int drambuf_start, drambuf_end, drambuf_chars;
#endif

/* Enable/Disable debugging */
#ifdef CONFIG_DTVLOGD_DEBUG

#define ddebug_info(fmt, args...) {		\
		printk(KERN_ERR			\
			"dtvlogd::%s " fmt,	\
			__func__, ## args);	\
}

#define ddebug_enter(void) {			\
		printk(KERN_ERR			\
			"dtvlogd::%s %d\n",	\
			__func__, __LINE__);	\
}

#define ddebug_err(fmt, args...) {			\
		printk(KERN_ERR				\
			"ERR:: dtvlogd::%s %d " fmt,	\
			__func__, __LINE__, ## args);	\
}

/* to test the logging into flash.
 * Sometimes, it's difficult to get the way to not reset RAM.
 * This can be used in such scenarios to verify the general
 * functionality of emergency logging without having to reboot
 */
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
int test_do_ram_flush(void)
{

	if (!ramwrite_start) {
		ddebug_err("DTVLOGD: Emergency log not started..\n");
		ddebug_err("Can't test RAM FLUSH..\n");
		return -1;
	}
	/* temporary emergency log file */
	RAM_FLUSH_FILE = "/tmp/emeg_log.txt";
	ram_clear_flush = 1;
	return 0;
}
#endif

#else /* CONFIG_DTVLOGD_DEBUG */
#define ddebug_err(fmt, args...) do {} while (0)
#define ddebug_info(fmt, args...) do {} while (0)
#define ddebug_enter(void) do {} while (0)
#endif

#define ddebug_print(fmt, args...) {		\
		printk(KERN_CRIT		\
			 fmt, ## args);	\
}

/*
 * macros for uid/gid for proc interface supported by dtvlogd
 *  - /proc/dtvlogd
 *  - /proc/dtvlogd_all
 *  - proc/dtvlogd_info/
 */
#define DTVLOGD_USER_ID 0 /* currently set as root */
#define DTVLOGD_GROUP_ID 506 /* dtvlogd: map with /etc/group */

/* required for dtvlogd_info  */
#define DTVLOGD_VERSION_MAX_STRING 10
#define DTVLOGD_BUFF_SIZE_MAX_STRING 12 /* sufficient for integer */
struct dtvlogd_info_buffer {
	size_t size;
	char data[0]; /* variable size buffer */
};

int opt_dtvlogd; /* Flag to track USB logging */

struct dtvlogd_buffer_info {
	/* the circular buffer start addres, this is not head/tail ptr. */
	unsigned long dtvlogd_add;
	int dtvlogd_len; /* length of the buffer */
	/* address of dlogbuf_end, most recent strings are kept at end */
	unsigned long dtvlogd_end_add;
	unsigned long dtvlogd_start_add; /* address of dlogbuf_start */
};

static struct dtvlogd_buffer_info  *dtvlogd_va_info;

static char __dlog_buf[__DLOG_BUF_LEN];
static char *dlog_buf = __dlog_buf;
static const int dlog_buf_len = __DLOG_BUF_LEN;

static int dlogbuf_start, dlogbuf_end;
static int dlogged_chars;
static int dlogbuf_writeok;
static int dlogbuf_writefail;

static DEFINE_SPINLOCK(dlogbuf_lock);
static DEFINE_SEMAPHORE(dlogbuf_sem);

static struct completion dlogbuf_completion;

wait_queue_head_t dlogbuf_wq;
int dlogbuf_complete;

/* for keeping track of the data to be printed on:
 * cat /proc/dtvlogd
 * cat /proc/dtvlogd_all
 */
struct proc_read_info {
	int is_open;       /* flag to avoid multiple callers */
	int is_init; /* flag for first read call */
	int is_clear_mode; /* mode for read operation - dtvlogd/dtvlogd_all */
	/* buffer indices and length */
	int start;
	int end;
	int len;
};

static struct proc_read_info dtvlogd_info, dtvlogd_all_info;
static int dlog_full;

#if defined(CONFIG_DTVLOGD_USB_LOG) || defined(CONFIG_DTVLOGD_EMERGENCY_LOG)
/* File path for logging */
#define LOGNAME_SIZE	64
char logname[LOGNAME_SIZE];
#endif

/* reference count is required in a scenario where
 * buffer is being read from parallel calls.
 */
static atomic_t dtvlogd_read_ref_cnt = ATOMIC_INIT(0);
static int dtvlogd_write_start_stop = 1; /* Flag to write DTVLOGD Buffer */

#ifdef CONFIG_KDEBUGD_HUB_DTVLOGD
int dtvlogd_buffer_printf(void);
#endif

void dtvlogd_write_start(void)
{
	int ref_cnt = 0;

	if (atomic_dec_and_test(&dtvlogd_read_ref_cnt)) {
		/* resume logging only when the last reader is finished. */
		ddebug_info("setting dtvlogd_write_start_stop to 1\n");
		dtvlogd_write_start_stop = 1;
	}

	ref_cnt = atomic_read(&dtvlogd_read_ref_cnt);

	WARN_ON(ref_cnt < 0);

	ddebug_info("write_start_stop: %d (should be: %d), ref_cnt: %d\n",
				dtvlogd_write_start_stop,
				ref_cnt ? 0 : 1,
				ref_cnt);
}

void dtvlogd_write_stop(void)
{
	/* we want to reset the flag always.
	 * this check is just to catch any error case
	 */
	if (atomic_read(&dtvlogd_read_ref_cnt) == 0)
		dtvlogd_write_start_stop = 0;

	atomic_inc(&dtvlogd_read_ref_cnt);

	ddebug_info("write_start_stop: %d (should be 0)\n",
			dtvlogd_write_start_stop);
}

#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
/*
 * Uninitialized memory operation
 */
static void write_ram_info(void)
{
	writel(drambuf_start, (void __iomem *)RAM_START_ADDR(ram_info_buf));
	writel(drambuf_end, (void __iomem *)RAM_END_ADDR(ram_info_buf));
	writel(drambuf_chars, (void __iomem *)RAM_CHARS_ADDR(ram_info_buf));
}

/*
 * dtvlogd=<add,size>
 * provides early dtvlogd init parameters.
 *
 * example,
 *    dtvlogd=0xFFFF0000,128k
 */
static int __init early_rambuf_init(char *arg)
{
	char *options = NULL;

	options = arg;
	if (!options)
		return -EINVAL;
	/* store the hex number present in dtvlogd_addr */
	emeg_log_addr = simple_strtoul(options, &options, 16);
	/* check for the next , in string */
	options = strnchr(options, strlen(options), ',');
	if (!options)
		return -EINVAL;
	options++;
	/* store the memory size in dtvlogd_size */
	emeg_log_len = memparse(options, &options);
	ddebug_info("emeg_log_addr 0x%x, emeg_log_len %d\n",
			(unsigned int)emeg_log_addr, emeg_log_len);
	return 0;
}
early_param("EME_BUFFINFO", early_rambuf_init);

/*
 * rambuf_init()
 */
static int rambuf_init(void)
{
	unsigned int magic = EMERGENCY_MAGIC_NUM;
	unsigned int temp;
	resource_size_t ram_buf_addr = 0;

	/* initialize the ram_buf variables from the configured addresses */
	ram_buf_addr = emeg_log_addr;
	dram_buf_len = (emeg_log_len >> 1);
	dram_buf_mask = (dram_buf_len - 1);

	ddebug_info("Emergency logging: Address: 0x%x, Size: %d, mask: %x\n",
			(unsigned int)ram_buf_addr, dram_buf_len,
			dram_buf_mask);
	/* Emergency log address should be PAGE aligned */
	if (!ram_buf_addr || (ram_buf_addr & ~(PAGE_MASK))) {
		ddebug_print("DTVLOGD: Emergency log address not configured correctly..\n");
		return -1;
	}

	if (!dram_buf_len || (dram_buf_len & dram_buf_mask)) {
		ddebug_print("DTVLOGD: Emergency log length should be power of 2\n");
		return -1;
	}

	ram_char_buf = ioremap(ram_buf_addr, dram_buf_len);
	ram_info_buf = ioremap(RAM_DATA_ADDR(ram_buf_addr), sizeof(u32) * 4);
	if (!ram_char_buf || !ram_info_buf) {
		ddebug_print("DTVLOGD: Error in ioremap for Emergency Log\n");
		return -1;
	}

	temp = (unsigned int)readl(ram_info_buf);

	ddebug_info("magic number read: %x (%x)\n", temp, magic);
	ram_log_magic = temp;
	if (magic == temp) {
		ram_clear_flush = 1;
	} else {
		ramwrite_start = 1;
		drambuf_start = drambuf_end = drambuf_chars = 0;
		writel(magic, ram_info_buf);
	}
	return 0;
}

static void ram_flush(void)
{
	char ram_logname[LOGNAME_SIZE];
	char *flush_buf;
	int len, i;
	int count = 0;
	int start, end, chars;
	struct file *fp;

	flush_buf = vmalloc(dram_buf_len);
	if (!flush_buf) {
		ddebug_err("error in allocating..\n");
		return;
	}

	ddebug_print("\n\n\n*******************************************\n");
	ddebug_print("*******************************************\n");
	ddebug_print("******** Emergency Log Backup *************\n");
	ddebug_print("*******************************************\n");
	ddebug_print("*******************************************\n\n\n\n");


	snprintf(ram_logname, LOGNAME_SIZE, RAM_FLUSH_FILE);
	ddebug_info("ram_logname: %s\n", RAM_FLUSH_FILE);

	fp = filp_open(ram_logname, O_CREAT|O_WRONLY|O_TRUNC|O_LARGEFILE, 0644);
	if (IS_ERR(fp))	{
		ddebug_err("error in opening log file\n");
		vfree(flush_buf);
		flush_buf = NULL;
		return;
	}

	spin_lock_irq(&dlogbuf_lock);

	start = (int)readl((void __iomem *)RAM_START_ADDR(ram_info_buf));
	end  = (int)readl((void __iomem *)RAM_END_ADDR(ram_info_buf));
	chars = (int)readl((void __iomem *)RAM_CHARS_ADDR(ram_info_buf));

	if (DRAM_INDEX(start) >= DRAM_INDEX(end))
		len = dram_buf_len - DRAM_INDEX(start);
	else
		len = DRAM_INDEX(end) - DRAM_INDEX(start);

	for (i = start ; i < (start + len) ; i++) {
		flush_buf[count] = (char)readb(ram_char_buf + DRAM_INDEX(i));
		count++;
	}

	if (DRAM_INDEX(start) >= DRAM_INDEX(end)) {
		for (i = 0 ; i < DRAM_INDEX(end) ; i++) {
			flush_buf[count] =
				(char)readb(ram_char_buf + DRAM_INDEX(i));
			count++;
		}
		len += DRAM_INDEX(end);
	}

	spin_unlock_irq(&dlogbuf_lock);

	if (fp->f_op && fp->f_op->write) {
		fp->f_op->write(fp, flush_buf, len, &fp->f_pos);
		ddebug_info("file written to flash\n");
	} else {
		ddebug_print("DTVLOGD: Error in writing emergency log\n");
	}

	vfree(flush_buf);
	flush_buf = NULL;
	filp_close(fp, NULL);

	ramwrite_start = 1;
	drambuf_start = drambuf_end = drambuf_chars = 0;

}
#endif


/*
 *	print_option()
 */
static void print_option(void)
{
	int mylen = dlog_buf_len;
	int mystart = dlogbuf_start;
	int myend = dlogbuf_end;
	int mychars = dlogged_chars;

	/* avoid below strings to be copied to dtvlogd ringbuffer */
	dtvlogd_write_stop();
	ddebug_print("\n\n\n");
	ddebug_print("==================================================\n");
	ddebug_print("= DTVLOGD v%s\n", DTVLOGD_VERSION);
	ddebug_print("==================================================\n");
#ifdef CONFIG_DTVLOGD_USB_LOG
	ddebug_print("USB Log File   = %s\n",
		(logname[0] == '/') ? logname : "(not found)");
	ddebug_print("Use DTVLOGD?   = %s\n", (opt_dtvlogd) ? "Yes" : "No");
	ddebug_print("Save Interval  = %d sec\n", USBLOG_SAVE_INTERVAL);
#else
	ddebug_print("DTVLOGD - USB Log : disabled by kernel config\n");
#endif
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
	ddebug_print("EMERG MAGIC    = 0x%x (0x%x)\n", ram_log_magic,
			EMERGENCY_MAGIC_NUM);
	ddebug_print("   RAM logging (address/len valid?)   = %s\n",
			(ramwrite_start) ? "Yes" : "No");
	ddebug_print("   ram_address: 0x%x (len: %d, total: %d)\n",
			(unsigned int)emeg_log_addr, dram_buf_len,
			emeg_log_len);
#endif
	ddebug_print("\n");

	ddebug_print("RingBuffer information:\n");
	ddebug_print("     Fixed VA: 0x%x\n", (unsigned)dtvlogd_va_info);
	ddebug_print("     Buffer Address: 0x%lx (len: %d)\n",
			dtvlogd_va_info->dtvlogd_add,
			dtvlogd_va_info->dtvlogd_len);
	ddebug_print("     End Index: %d, Start Index: %d\n",
			*(int *)(dtvlogd_va_info->dtvlogd_end_add),
			*(int *)(dtvlogd_va_info->dtvlogd_start_add));
	ddebug_print("     End Address: 0x%lx, Start Address: 0x%lx\n",
			dtvlogd_va_info->dtvlogd_end_add,
			dtvlogd_va_info->dtvlogd_start_add);
	ddebug_print("\n");

	ddebug_print("Buffer Size    = %d\n", mylen);
	ddebug_print("Start Index    = %d\n", mystart);
	ddebug_print("End Index      = %d\n", myend);
	ddebug_print("Saved Chars    = %d\n", mychars);
	ddebug_print("Write Ok       = %d\n", dlogbuf_writeok);
	ddebug_print("Write Fail     = %d\n", dlogbuf_writefail);
	ddebug_print("\n");
	dtvlogd_write_start();
}


#ifdef CONFIG_DTVLOGD_USB_LOG
/*
 *	USB I/O FUNCTIONS
 */
/*
 *	usblog_open()
 */
static struct file *usblog_open(void)
{
	struct file *fp;

	fp = filp_open(logname, O_CREAT|O_WRONLY|O_APPEND|O_LARGEFILE, 0666);

	return fp;
}

/*
 *	usblog_close()
 */
static int usblog_close(struct file *filp, fl_owner_t id)
{
	return filp_close(filp, id);
}

/*
 *	__usblog_write()
 */
static void __usblog_write(struct file *file, char *str, int len)
{
	file->f_op->write(file, str, (size_t)len, &file->f_pos);
}

/*
 *	usblog_write()
 */
static void usblog_write(char *s, int len)
{
	struct file *file;

	file = usblog_open();

	if (!IS_ERR(file)) {
		__usblog_write(file, s, len);
		usblog_close(file, NULL);
		ddebug_info("log flushed");
		dlogbuf_writeok++;
	} else {
		ddebug_err("error in opening USB file, disable usb logging\n");
		memset(logname, 0, LOGNAME_SIZE);
		opt_dtvlogd = 0;
		dlogbuf_writefail++;
	}
}

/*
 *	usblog_flush()
 *
 *	Flush all buffer message to usb.
 */
static void usblog_flush(void)
{
	static char flush_buf[__DLOG_BUF_LEN];
	int len = 0;

	if (dlogged_chars == 0) {
		ddebug_info("no characters logged\n");
		return;
	}

	if (!opt_dtvlogd) {
		ddebug_err("usb log not enabled\n");
		return;
	}

	spin_lock_irq(&dlogbuf_lock);

	if (DLOG_INDEX(dlogbuf_start) >= DLOG_INDEX(dlogbuf_end))
		len = dlog_buf_len - DLOG_INDEX(dlogbuf_start);
	else
		len = DLOG_INDEX(dlogbuf_end) - DLOG_INDEX(dlogbuf_start);

	memcpy(&flush_buf[0], &DLOG_BUF(dlogbuf_start), (size_t)len);

	if (DLOG_INDEX(dlogbuf_start) >= DLOG_INDEX(dlogbuf_end)) {
		memcpy(&flush_buf[len], &DLOG_BUF(0), DLOG_INDEX(dlogbuf_end));
		len += DLOG_INDEX(dlogbuf_end);
	}

	dlogbuf_start = dlogbuf_end;
	dlogged_chars = 0;

	spin_unlock_irq(&dlogbuf_lock);

	usblog_write(flush_buf, len);
}

static void dtvlogd_usblog_start(char *mount_dir)
{
	size_t len = strnlen(mount_dir, LOGNAME_SIZE - 1);
	ddebug_enter();

	ddebug_info("strlen(mount_dir) = %d\n", strlen(mount_dir));

	/* length can't be zero as its passed from user,
	 * added check just for prevent warning
	 *  - for negative offset in array
	 */
	if (!len)
		return;

	memset(logname, 0, LOGNAME_SIZE);
	strncpy(logname, mount_dir, len);

	if ((logname[len-1] < '0') || (logname[len-1] > 'z'))
		logname[len-1] = 0;

	/* We try to append the file-name to the directory
	 * provided by user. In case of error, full filename won't be written.
	 */
	strncat(logname, "/log.txt", LOGNAME_SIZE - len - 1);

	ddebug_info("setting usb log file: %s\n", logname);

	opt_dtvlogd = 1;
}

static void dtvlogd_usblog_stop(void)
{
	ddebug_enter();

	if (!opt_dtvlogd) {
		ddebug_print("usb logging is not running/already stopped\n");
		return;
	}

	do_dtvlog(5, NULL, 0);	/* flush the remained log */

	ddebug_info("reset usb log\n");
	memset(logname, 0, LOGNAME_SIZE);	/* remove mount location */
	opt_dtvlogd = 0;			/* disable usblog */
}
#endif /* CONFIG_DTVLOGD_USB_LOG */


#ifdef CONFIG_KDEBUGD_HUB_DTVLOGD
int dtvlogd_buffer_printf(void)
{
	int ret, i, validlen1 = 0, validlen2 = 0;
	char *dtvbuf1 = NULL, *dtvbuf2 = NULL;

	dtvlogd_write_stop();

	ret = acquire_dtvlogd_all_buffer(&dtvbuf1, &validlen1,
			&dtvbuf2, &validlen2);
	if (!ret) {
		ddebug_print("*******************************************\n");
		ddebug_print("******** Dtvlogd Log printing *************\n");
		ddebug_print("*******************************************\n");
		if (validlen1 > 0) {
			for (i = 0; i < validlen1; i++)
				printk("%c", dtvbuf1[i]);
		}
		if (validlen2 > 0) {
			for (i = 0; i < validlen2; i++)
				printk("%c", dtvbuf2[i]);
		}
		ddebug_print("********** Dtvlogd Log End ****************\n");
	} else {
		ddebug_print("****** Error in printing Dtvlogd Log ******\n");
	}

	dtvlogd_write_start();

	return 1;
}
#endif

static void dlogbuf_write(char c)
{
	/* this function gets called from printk and printf,
	 * thus ideally irq needs to be save and restored.
	 *
	 * overhead of locking is for every character sent for print
	 *
	 * if corruption occurs, it would be a few characters only,
	 * however, in testing, there hasn't been any corruption.
	 *
	 * If required, protect it using
	 * - spin_lock_irqsave(&dlogbuf_lock_i, flags);
	 * - spin_unlock_irqrestore(&dlogbuf_lock_i, flags);
	 */
	DLOG_BUF(dlogbuf_end) = c;

#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
	/*
	 * If RAM initialization fails, this flag will not be set.
	 * Info will not be written.
	 */
	if (ramwrite_start == 1) {
		writeb(c, ram_char_buf + DRAM_INDEX(drambuf_end));
		drambuf_end++;

		if (drambuf_end - drambuf_start > dram_buf_len)
			drambuf_start = drambuf_end - dram_buf_len;
		if (drambuf_chars < dram_buf_len)
			drambuf_chars++;

		write_ram_info();
	}
#endif

	dlogbuf_end++;

	if (!dlog_full) {
		if (dlogbuf_end > dlog_buf_len)
			dlog_full = 1;
	}

	if (dlogbuf_end - dlogbuf_start > dlog_buf_len)
		dlogbuf_start = dlogbuf_end - dlog_buf_len;
	if (dlogged_chars < dlog_buf_len)
		dlogged_chars++;
}

static int dtvlogd_print(const char *str, int len)
{
	const char *p;

	for (p = str; p < str + len && *p ; p++)
		dlogbuf_write(*p);

	return p - str;
}

#if defined(CONFIG_DTVLOGD_USB_LOG) || defined(CONFIG_DTVLOGD_EMERGENCY_LOG)
/*
 *	is_hw_reset()
 */

static int is_hw_reset(const char *str, int len)
{
	int i;
	/*
	 * power off (h/w reset) serial protocol
	 */
	static char hw_reset[9] = { 0xff, 0xff, 0x1d, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x1d };
	static char hw_reset2[9] = { 0xff, 0xff, 0x12, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x12 };

	if (len != 9)
		return 0;

	for (i = 0 ; i < 9 ; i++) {
		if ((str[i] != hw_reset[i]) && (str[i] != hw_reset2[i]))
			return 0;
	}

	ddebug_info("hw reset found\n");
	return 1;
}
#endif

/*
 * @str   : Serial string (printf, printk msg)
 * @count : Serial string count
 * @type  : 1->printf, 2->printk
 */

static int dtvlogd_write(const char __user *str, int count, int type)
{
	int len;

	char usr_buf[256];
	const char *buf = NULL;

	if (!dtvlogd_write_start_stop)
		return 1;

	if (type == 1) {
		/* printf */

		/* Prevent overflow */
		if (count > 256)
			count = 256;

		if (copy_from_user(usr_buf, str, (unsigned long)count)) {
			ddebug_err("printf::error\n");
			return -EFAULT;
		}

		buf = (const char *)usr_buf;
	} else if (type == 2) {
		/* printk */
		buf = (const char *)str;
	} else {
		BUG();
		return 0; /* Prevent warning fix */
	}

	/*
	 * Don't save micom command. It's not a ascii character.
	 * The exeDSP write a micom command to serial.
	 */

	if (!isascii(buf[0]) || count == 0) {
#if defined(CONFIG_DTVLOGD_USB_LOG) || defined(CONFIG_DTVLOGD_EMERGENCY_LOG)
		if (is_hw_reset(buf, count)) {
			ddebug_info("sending power off\n");
			/*
			 * flush the messages remaining in dlog buffer,
			 * and disable magic number
			 */
			do_dtvlog(5, NULL, 1); /* len=1 means power off */
		}
#endif
		return 1;
	}

	len = dtvlogd_print((const char *)buf, count);
	if (len < count)
		ddebug_info("Written %d of %d\n", len, count);

	return 1;
}

/*
 *	LOG FUNCTIONS
 */

/*
 *  Referenced do_syslog()
 *
 *  Commands to do_dtvlog:
 *
 *      3 -- Write printk messages to dlog buffer.
 *      4 -- Read and clear all messages remaining in the dlog buffer.
 *           This is called internally by dtvlogd.
 *           External callers should use command 5.
 *      5 -- Synchronously read and clear all messages remaining in
 *           the dlog buffer.
 *           This should be used by external callers as it provides protection.
 *      6 -- Read from ram buffer when booting time.
 *     10 -- Print number of unread characters in the dtvlog buffer.
 *     11 -- Write printf messages to dlog buffer.
 */
int do_dtvlog(int type, const char __user *buf, int len)
{
	int error = 0;
#ifdef CONFIG_DTVLOGD_USB_LOG
	int preempt;
#endif
#if defined(CONFIG_DTVLOGD_USB_LOG) || defined(CONFIG_DTVLOGD_EMERGENCY_LOG)
	int down_ret;
#endif

	switch (type) {
	case 3:
		/* Write some data to ring buffer. */
		dtvlogd_write(buf, len, 2);     /* from printk */
		break;
	case 4:
#ifdef CONFIG_DTVLOGD_USB_LOG
		/* actual usb flush is done in this function
		 * all the external users should use
		 * - do_dtvlog(5, ...)
		 */
		down_ret = down_interruptible(&dlogbuf_sem);
		if (!down_ret) {
			usblog_flush();
			up(&dlogbuf_sem);
		}

		if (dlogbuf_complete) {
			complete(&dlogbuf_completion);
			dlogbuf_complete = 0;
		}
#endif
		break;
	case 5:
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
		/*
		 * When die or panic was occurred, write magic number
		 *
		 *   - len = 0 : this call came from panic, die and stop
		 *   - len = 1 : this call came from power off
		 */
		if (len == 1) {
			ddebug_info("erasing magic number\n");
			writel(0xdead, ram_info_buf);
		}
#endif
#ifdef CONFIG_DTVLOGD_USB_LOG
		if (!opt_dtvlogd)
			break;

		DTVLOGD_PREEMPT_COUNT_SAVE(preempt);
		dlogbuf_complete = 1;

		/*
		 * this triggers the dtvlogd daemon to do actual usb flushing
		 */

		wake_up_interruptible(&dlogbuf_wq);

		wait_for_completion(&dlogbuf_completion);
		DTVLOGD_PREEMPT_COUNT_RESTORE(preempt);
#endif
		break;
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
	case 6:
		down_ret = down_interruptible(&dlogbuf_sem);
		if (!down_ret) {
			ram_flush();
			up(&dlogbuf_sem);
		}
		break;
#endif
	case 9:
		/* Number of chars in log buffer */
		error = dlogbuf_end - dlogbuf_start;
		break;
	case 10:
		/* Give the current configuration and
		 * print number of unread characters in the dtvlog buffer. */
		print_option();
		break;

	case 11:
		/* Write some data to ring buffer. */
		dtvlogd_write(buf, len, 1);     /* from printf */
		break;

	case 12:
#if defined(CONFIG_DTVLOGD_EMERGENCY_LOG) && defined(CONFIG_DTVLOGD_DEBUG)
		test_do_ram_flush();
#else
		ddebug_info("CONFIG_DTVLOGD_EMERGENCY_LOG needed for this feature..\n");
#endif
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;

}

/* allocate a page and map it to given virtual address */
static int dtvlogd_save_logaddr(void)
{
	/* this address is set in arch-specific fixmap.h */
	unsigned long vaddr = DTVLOGD_BUFFER_VIRTUAL_ADDRESS;
	struct page *p = alloc_page(GFP_KERNEL);
	if (!p) {
		ddebug_print("DTVLogd: error in allocting page\n");
		return -1;
	}

	if (ioremap_page_range(vaddr, vaddr + PAGE_SIZE,
			page_to_phys(p) , PAGE_KERNEL)) {
		ddebug_print("DTVLogd: error in mapping\n");
		return -1;
	}

	dtvlogd_va_info = (struct dtvlogd_buffer_info *)vaddr;
	dtvlogd_va_info->dtvlogd_add = (unsigned long) dlog_buf;
	dtvlogd_va_info->dtvlogd_len = dlog_buf_len;
	dtvlogd_va_info->dtvlogd_start_add = (unsigned long) &dlogbuf_start;
	dtvlogd_va_info->dtvlogd_end_add = (unsigned long) &dlogbuf_end;
	return 0;
}


/*
 *	dtvlogd()
 */
static int dtvlogd(void *unused)
{
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
	int ret;
	ret = rambuf_init();
	if (!ret) {
		ddebug_info("rambuf_init done\n");
	} else {
		ddebug_err("rambuf_init failed\n");
		ddebug_print("DTVLOGD: emergency log shall not work\n");
	}
#endif
	if (dtvlogd_save_logaddr()) {
		ddebug_print("DTVLogd can't be started without fixed va, ");
		ddebug_print("fix that problem first\n");
		return -1;
	}

	/* run after 15sec */
	wait_event_interruptible_timeout(dlogbuf_wq, dlogbuf_complete, 15*HZ);

	while (!kthread_should_stop()) {
#ifdef CONFIG_DTVLOGD_EMERGENCY_LOG
		if (ram_clear_flush == 1) {
			/*
			 * read from RAM buffer and create emergency log file
			 */
			do_dtvlog(6, NULL, 0);
			ram_clear_flush = 0;
		}
#endif

#ifdef CONFIG_DTVLOGD_USB_LOG
		if (opt_dtvlogd) {
			/* flush the dlog buffer to a USB file */
			do_dtvlog(4, NULL, 0);
		}
#endif

		wait_event_interruptible_timeout(dlogbuf_wq,
				dlogbuf_complete,
				USBLOG_SAVE_INTERVAL*HZ);
	}

	return 0;
}


/*
 * Below feature is not enabled currently.
 *  Enable this feature to support reading of DTVLOGD buffer
 *  from other kernel parts (e.g. coredump).
 *  Caller gets the copy of the buffer.
 *  Caller needs to allocate the buffer.
 */
#ifdef CONFIG_DTVLOGD_KERN_READ_CALLER_BUFFER
/** Get the size of the available dtvlogd buffer.
 *
 * This should be used to pass the argument to dtvlogd_get_buffer()
 *
 */
int dtvlogd_get_bufflen(void)
{
	return dlogged_chars;
}

/** Function to get the dtvlogd buffer from kernel
 *
 * pbuf: A valid buffer atleast count size
 * count: Size of the buffer to copy
 *
 * returns < 0  in case of error
 *         else no. of characters written
 */
int dtvlogd_get_buffer(char *pbuf, int count)
{
	int ret = -EINVAL;
	int len = count;
	int len1;

	/* some sanity */
	if (!pbuf || count < 0)
		goto out;
	ret = 0;
	if (!count || !dlogged_chars)
		goto out;

	dtvlogd_write_stop();
	spin_lock_irq(&dlogbuf_lock);

	/*
	 * the buffer might have been read from user/usblogging thread
	 */
	if (dlogged_chars < len)
		len = dlogged_chars;

	/* get the partial len 1 */
	if (DLOG_INDEX(dlogbuf_start) >= DLOG_INDEX(dlogbuf_end))
		len1 = dlog_buf_len - DLOG_INDEX(dlogbuf_start);
	else
		len1 = DLOG_INDEX(dlogbuf_end) - DLOG_INDEX(dlogbuf_start);

	if (len1 > len) {
		memcpy(&pbuf[0], &DLOG_BUF(dlogbuf_start), len);
	} else {
		memcpy(&pbuf[0], &DLOG_BUF(dlogbuf_start), len1);
		memcpy(&pbuf[len1], &DLOG_BUF(0), (len - len1));
	}

	ret = len;
	spin_unlock_irq(&dlogbuf_lock);
	dtvlogd_write_start();
out:

	return ret;
}
#endif

#ifdef CONFIG_BINFMT_ELF_COMP
/*
 * Provide the dtvlogd buffer to the caller.
 * It has two parts:
 * buf1: Start address having len1 as valid length.
 * buf2: Remaining part of buffer having len2 as valid len.
 */
int acquire_dtvlogd_buffer(char **buf1, int *len1, char **buf2, int *len2)
{
	WARN_ON(!buf1 || !buf2 || !len1 || !len2);

	/* sanitize the arguments */
	if (!buf1 || !buf2 || !len1 || !len2) {
		ddebug_print("DTVLOGD Err:%s:: Invalid arguments buf1: %p, len1: %p, buf2: %p, len2: %p\n",
				__func__, buf1, len1, buf2, len2);
		return -1;
	}

	if (!dlogged_chars) {
		*len1 = 0;
		*len2 = 0;
		return 0;
	}

	spin_lock_irq(&dlogbuf_lock);

	*buf1 = &DLOG_BUF(dlogbuf_start);
	if (DLOG_INDEX(dlogbuf_start) >= DLOG_INDEX(dlogbuf_end)) {
		*len1 = dlog_buf_len - DLOG_INDEX(dlogbuf_start);
		*len2 = DLOG_INDEX(dlogbuf_end);
		*buf2 = &DLOG_BUF(0);
	} else {
		*len1 = DLOG_INDEX(dlogbuf_end) - DLOG_INDEX(dlogbuf_start);
		*len2 = 0;
	}

	spin_unlock_irq(&dlogbuf_lock);

	return 0;
}

/*
 * Provide the "dtvlogd_all" buffer to the caller.
 * It has two parts:
 * buf1: Start address having len1 as valid length.
 * buf2: Remaining part of buffer having len2 as valid len.
 */
int acquire_dtvlogd_all_buffer(char **buf1, int *len1, char **buf2, int *len2)
{
	int start, end;

	WARN_ON(!buf1 || !buf2 || !len1 || !len2);

	/* sanitize the arguments */
	if (!buf1 || !buf2 || !len1 || !len2) {
		ddebug_print("DTVLOGD Err:%s:: Invalid arguments buf1: %p, len1: %p, buf2: %p, len2: %p\n",
				__func__, buf1, len1, buf2, len2);
		return -1;
	}

	spin_lock_irq(&dlogbuf_lock);

	end = DLOG_INDEX(dlogbuf_end);

	if (!dlog_full) {
		/* start from 0 to end */
		start = 0;
		*buf1 = &DLOG_BUF(start);
		*len1 = end;
		*len2 = 0;
	} else {
		/*
		 * we need to read full buffer, so start = end
		 *  1) end to dlog_buf_len
		 *  2) 0 to end
		 */
		start = end;
		*buf1 = &DLOG_BUF(start);
		*len1 = dlog_buf_len - DLOG_INDEX(start);
		*buf2 = &DLOG_BUF(0);
		*len2 = DLOG_INDEX(end);
	}

	spin_unlock_irq(&dlogbuf_lock);

	return 0;
}

#endif

static void print_dtvlogd_usage(void)
{
	ddebug_print("\nUsage(DTVLOGD):\n");
	ddebug_print("  echo 1 > /proc/dtvlogd             # show info\n");
#ifdef CONFIG_DTVLOGD_USB_LOG
	ddebug_print("  echo 2 > /proc/dtvlogd             # flush log to usb\n");
	ddebug_print("  echo /dtv/usb/sda1 > /proc/dtvlogd # start usb log\n");
	ddebug_print("  echo 4 > /proc/dtvlogd             # stop usb log\n");
#endif
#ifdef CONFIG_DTVLOGD_DEBUG
	ddebug_print("  echo 5 > /proc/dtvlogd             # debug commmand\n");
#endif
}

#define MAX_DMSG_WRITE_BUFFER	64

static ssize_t proc_dtvlogd_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[MAX_DMSG_WRITE_BUFFER];
	long idx;

	if (!count)
		return (ssize_t)count;

	if (count >= MAX_DMSG_WRITE_BUFFER)
		count = MAX_DMSG_WRITE_BUFFER - 1;

	/*
	 * Prevent Tainted Scalar Warning:
	 * Buffer can't be tainted because:
	 * 1. The count never exceeds MAX_DMSG_WRITE_BUFFER i.e. buffer size.
	 * 2. copy_from_user returns 0 in case of correct copy.
	 * So, we don't need to sanitize buffer.
	 */
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';

	/* rearrange, for if , else if, we should have final else */
	if (kstrtol(buffer, 0, &idx) != 0) {
		if (buffer[0] == '/') {
			idx = 3;
		} else {
			print_dtvlogd_usage();
			return -EINVAL;
		}
	}

	ddebug_info("Command to dtvlogd: idx = %ld\n", idx);  /* for debug */

	switch (idx) {
	case 1:
		/* print the dtvlogd status information */
		do_dtvlog(10, NULL, 0);
		break;

#ifdef CONFIG_DTVLOGD_USB_LOG
	case 2:
		/* flush the remaining buffer to USB */
		do_dtvlog(5, NULL, 0);
		break;
	case 3:
		/* start usb logging */
		dtvlogd_usblog_start(buffer);
		break;
	case 4:
		/* stop usb logging */
		dtvlogd_usblog_stop();
		break;
#endif

#ifdef CONFIG_DTVLOGD_DEBUG
	case 5:
		/* This is used to flush RAM during run time */
		do_dtvlog(12, NULL, 0);
		break;
#endif

	default:
		print_dtvlogd_usage();
		break;
	}

	return (ssize_t)count;
}

static int proc_dtvlogd_release(struct inode *inode, struct file *filp)
{
	struct proc_read_info *proc_info = filp->private_data;
	WARN_ON(!proc_info->is_open);

	ddebug_info("called for %s (%p)\n", proc_info == &dtvlogd_info
			? "dtvlogd" : "dtvlogd_all",
			proc_info);

	proc_info->is_open = 0;

	/* close without reading all */
	if (proc_info->is_init)
		dtvlogd_write_start();

	filp->private_data = NULL;
	return 0;
}

static ssize_t proc_dtvlogd_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	char ch;
	int ret;
	int read_len;
	int start, end, len;
	struct proc_read_info *proc_info = filp->private_data;

	WARN_ON(!proc_info->is_open);

	if (!proc_info->is_init) {
		proc_info->is_init = 1;
		/* call stop() only once */
		dtvlogd_write_stop();
	}
	start = proc_info->start;
	end = proc_info->end;
	len = proc_info->len;

	ddebug_info("called for %s (%p)\n",
			proc_info == &dtvlogd_info ? "dtvlogd" : "dtvlogd_all",
			proc_info);
	ddebug_info("buffer start: %d, end: %d, chars: %d(to read: %d)\n",
			start, end, len, count);

	ret = -EINVAL;

	if (!buf)
		goto out;

	ret = 0;
	if (!count)
		goto out;

	if (!access_ok(VERIFY_WRITE, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	read_len = 0;
	spin_lock_irq(&dlogbuf_lock);

	/* loop the DTVLOGD buffer for no. of characters logged
	 * from start to end.
	 *
	 *  - Read the characters from buffer in ch and copy to user
	 *  - Log the no. of characters copied into read_len
	 *  - in case of error, return negative.
	 */
	while (!ret && (start != end) && (size_t)read_len < count) {
		ch = DLOG_BUF(start);
		start++;
		len--;
		spin_unlock_irq(&dlogbuf_lock);
		ret = __put_user(ch, buf);
		buf++;
		read_len++;
		cond_resched();
		spin_lock_irq(&dlogbuf_lock);
	}
	spin_unlock_irq(&dlogbuf_lock);
	/* No error, return the no. of characters read so far */
	if (!ret)
		ret = read_len;

	proc_info->start = start;
	proc_info->len = len;

	/* update the buffer indices for clear mode */
	if (proc_info->is_clear_mode) {
		dlogbuf_start = proc_info->start;
		dlogged_chars = len;
	}

out:
	if (ret <= 0) {
		ddebug_info("no more characters (%d)...\n", ret);
		dtvlogd_write_start(); /* resume logging */
		proc_info->is_init = 0;
	}

	return ret;
}



static ssize_t proc_dtvlogd_open(struct inode *inode, struct file *filp)
{
	if (dtvlogd_info.is_open)
		return -EACCES;

	dtvlogd_info.is_open = 1;
	dtvlogd_info.is_init = 0;
	dtvlogd_info.is_clear_mode = 1;
	dtvlogd_info.start = dlogbuf_start;
	dtvlogd_info.end = dlogbuf_end;
	dtvlogd_info.len = dlogged_chars;
	filp->private_data = &dtvlogd_info;
	return 0;
}

static ssize_t proc_dtvlogd_all_open(struct inode *inode, struct file *filp)
{
	if (dtvlogd_all_info.is_open)
		return -EACCES;

	dtvlogd_all_info.is_open = 1;
	dtvlogd_all_info.is_init = 0;
	dtvlogd_all_info.is_clear_mode = 0;
	/*
	 * To get the new buffer, user should call open()/read()/close().
	 *
	 * We fetch the buffer details at the time of open
	 * Caller should read the buffer fast, and should call close
	 * This way, we keep the buffer limit of dlog_buf_len (128KB)
	 */

	dtvlogd_all_info.end = dlogbuf_end;

	if (!dlog_full) {
		dtvlogd_all_info.start = 0;
		dtvlogd_all_info.len = DLOG_INDEX(dtvlogd_all_info.end);
	} else {
		dtvlogd_all_info.len = dlog_buf_len;
		dtvlogd_all_info.start = dlogbuf_end - dlog_buf_len;
	}
	filp->private_data = &dtvlogd_all_info;

	return 0;
}

/* Define the operation allowed on dtvlogd
 * We just use:
 *   proc_dtvlogd_write: To give commands (e.g. echo 1 > /proc/dtvlogd)
 *   proc_dtvlogd_open: To prepare buffer for read() operation.
 *   proc_dtvlogd_read: To print the log accumulated by dtvlogd.
 *
 */
static const struct file_operations proc_dtvlogd_ops = {
	.open       = proc_dtvlogd_open,
	.read       = proc_dtvlogd_read,
	.write      = proc_dtvlogd_write,
	.release    = proc_dtvlogd_release,

};

/** File operations for /proc/dtvlogd_all
 *
 * proc_dtvlogd_all_open: prepares the buffer for read()
 * proc_dtvlogd_read: to copy the buffer
 */
static const struct file_operations proc_dtvlogd_all_ops = {
	.open       = proc_dtvlogd_all_open,
	.read       = proc_dtvlogd_read,
	.release    = proc_dtvlogd_release,

};

static ssize_t proc_dtvlogd_info_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	/*
	 * get the buffer pointer saved during init_procfs_dtvlogd_info()
	 * and return to user
	 * : after 3.10, use PDE_DATA and file_inode macros
	 */
	struct dtvlogd_info_buffer *info_buff;
    info_buff = PDE_DATA(file_inode(file));

	return simple_read_from_buffer(buf, count, ppos,
			info_buff->data, info_buff->size);
}

static const struct file_operations proc_dtvlogd_info_ops = {
	.read = proc_dtvlogd_info_read,
};

/*
 * init_procfs_dtvlogd_info: Read info proc interface
 *  /proc/dtvlogd_info/version:
 *     Read interface: Print the dtvlogd version e.g. 5.1.1
 *  /proc/dtvlogd_info/buff_size:
 *    Read interface: Return the dtvlogd buffer e.g. 262144
 */
static int  init_procfs_dtvlogd_info(uid_t uid, gid_t gid)
{
	struct proc_dir_entry *info_dir = NULL; /* /proc/dtvlogd_info entry */
	struct proc_dir_entry *entry = NULL;
	struct dtvlogd_info_buffer *dtvlogd_info_version = NULL, *dtvlogd_info_buff_size = NULL;
	int ret;

	info_dir = proc_mkdir("dtvlogd_info", NULL);
	if (!info_dir) {
		ddebug_err("error in proc_mkdir\n");
		return -1;
	}

	proc_set_user(info_dir, uid, gid);

	dtvlogd_info_version = kzalloc(sizeof(*dtvlogd_info_version) + DTVLOGD_VERSION_MAX_STRING,
			GFP_KERNEL);
	if (!dtvlogd_info_version) {
		ddebug_err("error in allocating memory\n");
		goto err_info_version;
	}

	dtvlogd_info_version->size = DTVLOGD_VERSION_MAX_STRING;
	ret = snprintf(dtvlogd_info_version->data, DTVLOGD_BUFF_SIZE_MAX_STRING, "%s\n",
			DTVLOGD_VERSION);
	if ((ret < 0) || (ret >= DTVLOGD_BUFF_SIZE_MAX_STRING)) {
		ddebug_print("DTVLogd: error in snprintf, ret: %d, size: %d\n",
				ret, DTVLOGD_BUFF_SIZE_MAX_STRING);
		goto err_info_version;
	}

	/* read-only permission for user/group (0440) */
	entry = proc_create_data("version", 0440, info_dir,
			&proc_dtvlogd_info_ops, dtvlogd_info_version);
	if (!entry) {
		ddebug_err("error in creating dtvlogd_info/version\n");
		goto err_info_version;
	}

	proc_set_user(entry, uid, gid);

	dtvlogd_info_buff_size = kzalloc(sizeof(*dtvlogd_info_buff_size)
			+ DTVLOGD_BUFF_SIZE_MAX_STRING, GFP_KERNEL);
	if (!dtvlogd_info_buff_size) {
		ddebug_err("error in allocating memory\n");
		goto err_info_buff_size;
	}

	dtvlogd_info_buff_size->size = DTVLOGD_BUFF_SIZE_MAX_STRING;
	ret = snprintf(dtvlogd_info_buff_size->data, DTVLOGD_BUFF_SIZE_MAX_STRING, "%d\n",
			dlog_buf_len);
	if ((ret < 0) || (ret >= DTVLOGD_BUFF_SIZE_MAX_STRING)) {
		ddebug_print("DTVLogd: error in snprintf, ret: %d, size: %d\n",
				ret, DTVLOGD_BUFF_SIZE_MAX_STRING);
		goto err_info_buff_size;
	}

	/* read-only permission for user/group (0440) */
	entry = proc_create_data("buff_size", 0440, info_dir,
			&proc_dtvlogd_info_ops, dtvlogd_info_buff_size);
	if (!entry) {
		ddebug_err("error in creating dtvlogd_info/version\n");
		goto err_info_buff_size;
	}

	proc_set_user(entry, uid, gid);

	return 0;

err_info_buff_size:
	/* in case of NULL, kfree() is NOP */
	kfree(dtvlogd_info_buff_size);
	remove_proc_entry("version", info_dir);
err_info_version:
	kfree(dtvlogd_info_version);
	/* remove the /proc/dtvlogd_info directory in case of error */
	remove_proc_entry("dtvlogd_info", NULL);
	return -1;
}
/*
 * Create Proc Entry
 */
static int __init init_procfs_msg(void)
{

	const uid_t dtvlogd_user = DTVLOGD_USER_ID;
	const gid_t dtvlogd_group = DTVLOGD_GROUP_ID;
	struct proc_dir_entry *dtvlogd_entry, *dtvlogd_all_entry;

	/* read/write permission for user/group (0660) */
	dtvlogd_entry = proc_create("dtvlogd", 0660, NULL,
			&proc_dtvlogd_ops);
	if (!dtvlogd_entry)
		goto err;

	proc_set_user(dtvlogd_entry, dtvlogd_user, dtvlogd_group);

	/* read permission for user/group (0440) */
	dtvlogd_all_entry = proc_create("dtvlogd_all", 0440, NULL,
			&proc_dtvlogd_all_ops);
	if (!dtvlogd_all_entry)
		goto err;

	proc_set_user(dtvlogd_all_entry, dtvlogd_user, dtvlogd_group);

	if (init_procfs_dtvlogd_info(dtvlogd_user, dtvlogd_group))
		ddebug_print("DTVLogd: Error in exporting dtvlogd info\n");

	/* dtvlogd can still continue even if dtvlogd_info fails,
	 * so, just print error, return success */
	return 0;
err:
	if (dtvlogd_entry)
		remove_proc_entry("dtvlogd", NULL);

	return -1;
}

/*
 *	dtvlogd_init()
 */

static int __init dtvlogd_init(void)
{
	struct task_struct *task;
	int ret;

	ret = init_procfs_msg();
	if (ret) {
		ddebug_print("DTVLOGD: unable to create proc interface\n");
		return -1;
	}

	init_completion(&dlogbuf_completion);

	init_waitqueue_head(&dlogbuf_wq);

	task = kthread_run(dtvlogd, NULL, "kdtvlogd");
	if (IS_ERR(task)) {
		ddebug_print("DTVLogd: unable to create kernel thread: %ld\n",
				PTR_ERR(task));
		return PTR_ERR(task);
	}

	/* no need to call dtvlogd_write_start(). This is already started.
	 * as flag is set to 1 in the beginning,
	 * since we want logs before dtvlogd_init too */
	return 0;
}

module_init(dtvlogd_init);
