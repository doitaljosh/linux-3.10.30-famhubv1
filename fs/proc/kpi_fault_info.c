#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#define TASK_COMM_LEN_BUF	256

struct timespec kpi_fault_time;
unsigned long kpi_fault_pc;
unsigned long kpi_fault_lr;
char kpi_fault_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
char kpi_fault_version_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
char kpi_fault_process_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};

struct timespec kpi_fault_3rd_time;
unsigned long kpi_fault_3rd_pc;
unsigned long kpi_fault_3rd_lr;
char kpi_fault_info_status[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
char kpi_fault_3rd_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
char kpi_fault_3rd_version_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
char kpi_fault_3rd_process_comm[TASK_COMM_LEN_BUF]={'N','O','N','E','\0'};
 
void clear_kpi_fault_non_allowed(void);
void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name);
void set_kpi_fault_3rd(unsigned long pc, unsigned long lr, char* thread_name, char* process_name);

void clear_kpi_fault_non_allowed(void)
{
	kpi_fault_3rd_time.tv_sec = 0;
	kpi_fault_3rd_time.tv_nsec = 0;
	kpi_fault_3rd_pc = 0;
	kpi_fault_3rd_lr = 0;
	strncpy(kpi_fault_3rd_comm, "NONE", 5);
	strncpy(kpi_fault_3rd_process_comm, "NONE", 5);
}

void set_kpi_fault(unsigned long pc, unsigned long lr, char* thread_name, char* process_name)
{
	do_posix_clock_monotonic_gettime(&kpi_fault_time);
	monotonic_to_bootbased(&kpi_fault_time);

	kpi_fault_pc = pc;
	kpi_fault_lr = lr;

	strncpy(kpi_fault_comm, thread_name, TASK_COMM_LEN_BUF);
	strncpy(kpi_fault_process_comm, process_name, TASK_COMM_LEN_BUF);
}

void set_kpi_fault_3rd(unsigned long pc, unsigned long lr, char* thread_name, char* process_name)
{
	do_posix_clock_monotonic_gettime(&kpi_fault_3rd_time);
	monotonic_to_bootbased(&kpi_fault_3rd_time);

	kpi_fault_3rd_pc = pc;
	kpi_fault_3rd_lr = lr;

	strncpy(kpi_fault_3rd_comm, thread_name, TASK_COMM_LEN_BUF);
	strncpy(kpi_fault_3rd_process_comm, process_name, TASK_COMM_LEN_BUF);
}

static int kpi_fault_proc_show(struct seq_file *m, void *v)
{
	/* print process name : "X" => "XServer" */
	if(!strncmp(kpi_fault_process_comm, "X", TASK_COMM_LEN_BUF))
	{
		strncpy(kpi_fault_process_comm, "XServer", 8);
	}

	seq_printf(m,"%lu.%02lu|%s|0x%08lx|0x%08lx|%s|%s|%s|dummy\n",
			(unsigned long) kpi_fault_time.tv_sec,
			(kpi_fault_time.tv_nsec / (NSEC_PER_SEC / 100)),
			kpi_fault_comm,	kpi_fault_pc, kpi_fault_lr, kpi_fault_process_comm, kpi_fault_info_status, kpi_fault_version_comm);

	return 0;
}

static int kpi_fault_proc_3rd_show(struct seq_file *m, void *v)
{
	seq_printf(m,"%lu.%02lu|%s|0x%08lx|0x%08lx|%s|%s|%s|dummy\n",
			(unsigned long) kpi_fault_3rd_time.tv_sec,
			(kpi_fault_3rd_time.tv_nsec / (NSEC_PER_SEC / 100)),
			kpi_fault_3rd_comm,	kpi_fault_3rd_pc, kpi_fault_3rd_lr, kpi_fault_3rd_process_comm, kpi_fault_info_status,kpi_fault_3rd_version_comm);
	return 0;
}

#define	BUF_SIZE	256
static ssize_t kpi_fault_info_3rd_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[BUF_SIZE];
	long idx;

	if (!count)
		return (ssize_t)count;

	if (count >= BUF_SIZE)
		count = BUF_SIZE - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';

	if (kstrtol(buffer, 0, &idx) != 0) {
		if (buffer[0] == '/') {
			idx = 3;
		} else {
			return -EINVAL;
		}
	}

	switch (idx) {
		case 0:
			clear_kpi_fault_non_allowed();
			break;
		default:
			printk("\nUsage : For reset\necho 0 > /proc/kpi_fault_info_3rd \n");
			break;
	}
	return (ssize_t)count;
}
static ssize_t kpi_fault_info_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
        char buffer[BUF_SIZE];

        if (!count)
                return (ssize_t)count;

        if (count >= BUF_SIZE)
                count = BUF_SIZE - 1;

        if (copy_from_user(buffer, buf, count))
                return -EFAULT;

        buffer[BUF_SIZE-1] = '\0';

	sscanf(buffer,"%s %s %s",kpi_fault_info_status, kpi_fault_process_comm, kpi_fault_version_comm);	
	memcpy(kpi_fault_comm, kpi_fault_process_comm, TASK_COMM_LEN_BUF);
	
	return (ssize_t)count;
}

static int kpi_fault_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, kpi_fault_proc_show, NULL);
}

static int kpi_fault_info_3rd_open(struct inode *inode, struct file *file)
{
	return single_open(file, kpi_fault_proc_3rd_show, NULL);
}

static const struct file_operations kpi_fault_info_fops = {
	.open  = kpi_fault_info_open,
	.read  = seq_read,
	.write  = kpi_fault_info_write,
};

static const struct file_operations kpi_fault_info_3rd_fops = {
	.open  = kpi_fault_info_3rd_open,
	.read  = seq_read,
	.write = kpi_fault_info_3rd_write,
};

static int __init proc_kpi_fault_info_init(void)
{
	proc_create("kpi_fault_info", 0, NULL, &kpi_fault_info_fops);
	proc_create("kpi_fault_info_3rd", 0666, NULL, &kpi_fault_info_3rd_fops);
	return 0;
}
module_init(proc_kpi_fault_info_init);
