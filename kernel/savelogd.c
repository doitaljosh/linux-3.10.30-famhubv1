
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/kernel_stat.h>

#define SAVE_LOG_NAME            "save_log"
#define SAVE_RESULT		"/tmp/save_done"
#define SAVE_LOG_MINOR           251

#define WRITE_OK		911
#define NAME_BUFSIZE		256
#define SEND_BUFSIZE		256
#define MAX_COUNT		1000

static struct mutex savelogd_lock;
int savelogbuf_pid= -1;
int savelogbuf_status=0;
char * save_thread_name;

static ssize_t save_read(struct file *filp, char __user *buf,
                size_t count, loff_t *ppos)
{

        char pid_buf[SEND_BUFSIZE];
	int len = 0;

	mutex_lock(&savelogd_lock);
	if(savelogbuf_pid == -1)
	{
		sprintf(pid_buf, "%d%c",0,'\0');
		len = (int)strlen(pid_buf);
		if(copy_to_user(buf,pid_buf,(long unsigned int)len))
		{
			mutex_unlock(&savelogd_lock);
			return -EFAULT;
		}
	}
	else
	{
		snprintf(pid_buf, 256,"%c %d %s%c",savelogbuf_status,savelogbuf_pid, save_thread_name,'\0');
		len = (int)strlen(pid_buf);
		if(copy_to_user(buf,pid_buf,(long unsigned int)len))
		{
			mutex_unlock(&savelogd_lock);
			return -EFAULT;
		}
		savelogbuf_pid = -1;
		savelogbuf_status = 0;
	}
	mutex_unlock(&savelogd_lock);
        return len;
}

ssize_t save_write2(struct file *filp, const char __user *buf,
                size_t count, loff_t *ppos)
{

        int value = 1;
        char status=0;
        char magic;
        char set_data[SEND_BUFSIZE];
        int file_save_result = 0;
        struct stat64 statbuf;
        mm_segment_t old_fs;
        int i,fd;

        memcpy(set_data,buf,256);

        if(set_data[0] != 'Z')
        {
                return 1;
        }
        set_data[SEND_BUFSIZE-1]='\0';

        sscanf(set_data,"%c %c %d %s",&magic,&status, &value, save_thread_name);
        if((status == 'w') || (status == 'r') || (status == 'a') || (status == 'd')) {
                        mutex_lock(&savelogd_lock);
                        savelogbuf_pid = value;
                        savelogbuf_status = status;
                        mutex_unlock(&savelogd_lock);
        }

        old_fs = get_fs();
        set_fs(KERNEL_DS);
        memset(&statbuf,0,sizeof(statbuf));

        for(i=0;i<MAX_COUNT;i++)
        {
                sys_stat64(SAVE_RESULT, &statbuf);
                if(statbuf.st_size==0)
                {
                        msleep(10);
                        continue;
                }
                if((fd=sys_open(SAVE_RESULT, O_RDONLY, 0 ) )>= 0)
                {
                        file_save_result = 1;
                        break;
                }
        }

        if(file_save_result)
        {
                sys_close((unsigned int)fd);
                sys_unlink(SAVE_RESULT);
        }
        else
        {
                printk(KERN_ERR"save log fail\n");
        }
        set_fs(old_fs);

        return 1;
}
EXPORT_SYMBOL(save_write2);

static ssize_t save_write(struct file *filp, const char __user *buf,
                size_t count, loff_t *ppos)
{

        int value = 1;
	char status=0;
	char magic;
	char set_data[SEND_BUFSIZE];
	int file_save_result = 0;
	struct stat64 statbuf;
	mm_segment_t old_fs;
	int i,fd;
	
        if(copy_from_user(set_data,buf,256))
                return -EFAULT;

	if(set_data[0] != 'Z')
	{
		return 1;
	}
	set_data[SEND_BUFSIZE-1]='\0';

        sscanf(set_data,"%c %c %d %s",&magic,&status, &value, save_thread_name);
        if((status == 'w') || (status == 'r') || (status == 'a') || (status == 'd')) {
			mutex_lock(&savelogd_lock);
			savelogbuf_pid = value;
			savelogbuf_status = status;
			mutex_unlock(&savelogd_lock);
        }

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	memset(&statbuf,0,sizeof(statbuf));

	for(i=0;i<MAX_COUNT;i++)
        {
	        sys_stat64(SAVE_RESULT, &statbuf);
	        if(statbuf.st_size==0)
	        {
                	msleep(10);
			continue;
	        }
		if((fd=sys_open(SAVE_RESULT, O_RDONLY, 0 ) )>= 0)
                {
                        file_save_result = 1;
                        break;
                }
        }

        if(file_save_result)
        {
                sys_close((unsigned int)fd);
                sys_unlink(SAVE_RESULT);
        }
        else
        {
                printk(KERN_ERR"save log fail\n");
        }
	set_fs(old_fs);

        return 1;
}

static int save_open(struct inode *inode, struct file *file)
{
        return 0;
}


static const struct file_operations save_log_fops = {
        .owner = THIS_MODULE,
        .open  = save_open,
        .read  = save_read,
        .write = save_write,
};

static struct miscdevice save_log_dev = {
        .minor = SAVE_LOG_MINOR,
        .name = SAVE_LOG_NAME,
        .fops = &save_log_fops, 
};


static int __init save_log_init(void)
{
        int ret_val = 0;

	mutex_init(&savelogd_lock);

	save_thread_name = kmalloc(NAME_BUFSIZE, GFP_KERNEL);

	if (!save_thread_name) {
		printk(KERN_ERR"[SAVE_LOG] malloc failed\n");
	}

        ret_val = misc_register(&save_log_dev);
        if(ret_val){
                printk(KERN_ERR "[ERR]%s: misc register failed\n", SAVE_LOG_NAME);
        }
        else {
                printk(KERN_INFO"[SAVE_LOG] %s initialized\n", SAVE_LOG_NAME);
        }
        return ret_val;
}
static void __exit save_log_exit(void)
{
        misc_deregister(&save_log_dev);
        return;
}
module_init(save_log_init);
module_exit(save_log_exit);
