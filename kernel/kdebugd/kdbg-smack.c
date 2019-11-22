#ifdef CONFIG_KDEBUGD_AGENT_SMACK_DISABLE
#include <kdebugd/kdebugd.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#define KDBG_SMACK_CONF_NAME "kdbgd/smack-control"
#define TMP_SIZE 10

unsigned long kdbg_smack_enable = 1;
EXPORT_SYMBOL(kdbg_smack_enable);

/*auto key test activate*/
static const struct file_operations kdbg_smack_conf = {
	.read = kdbg_smack_conf_read,
	.write = kdbg_smack_conf_write
};

int kdbg_smack_control(void)
{
	static struct proc_dir_entry *kdbg_smk_control;
	int err = 0;
	if (!kdbg_smk_control) {
		kdbg_smk_control = proc_create(KDBG_SMACK_CONF_NAME, 0777,
				NULL, &kdbg_smack_conf);
		if (!kdbg_smk_control) {
			printk(KERN_WARNING
					"/proc/kdbgd/smack-control creation failed\n");
			err = -ENOMEM;
		}
	}
	return err;
}

/**
 * This function is called with the /proc file is read
 *
 */
int kdbg_smack_conf_read(struct file *pfile, char __user *buffer, size_t count, loff_t *offset)
{
	long ret;
	long size;
	static int flag;
	char tmp_buf[TMP_SIZE];

	if (flag) {
		flag = 0;
		return 0;
	}

	memset(tmp_buf, 0, TMP_SIZE);
	size = snprintf(tmp_buf, TMP_SIZE, "%lu\n", kdbg_smack_enable);

	/* fill the buffer, return the buffer size */
	if (copy_to_user(buffer, (void *)tmp_buf, size))
		return -EFAULT;

	flag = 1;
	ret = size;

	return ret;
}

/**
 * This function is called with the /proc file is written
 *
 */
int kdbg_smack_conf_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *data)
{
	unsigned long ret;
	unsigned int base = 10;
	char temp_buf[TMP_SIZE];

	if (count != 2)
		return -1;

	if (copy_from_user(temp_buf, buffer, count))
		return -EFAULT;

	temp_buf[count - 1] = '\0';
	if (kstrtol(temp_buf, base, &kdbg_smack_enable) < 0)
		return -1;

	ret = count;

	/* kdbg_smack_enable value could be either 1 or 0
	 * kdbg_smack_enable = 1 apply smack
	 * kdbg_smack_enable = 0 restrict smack
	 * */
	if (kdbg_smack_enable > 1)
		return -1;

	return (int)ret;
}

#endif /* CONFIG_KDEBUGD_AGENT_SMACK_DISABLE */
