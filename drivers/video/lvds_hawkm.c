#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/of_device.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/delay.h>

#define	DEVICE_NAME   		"lvds_hawkm"

#define		TRUE	(1)
#define		FALSE	(0)

#define REG_LVDS_CTRL		(0x5C6004)
#define REG_TMG				(0xC10000)

#define GPIO_LCD_EN			(0x0103)


extern int set_gpio_level(int gpio, int level);

static dev_t gDev_major_num=0;
static struct cdev gLvds_hawkm_cdev;
static struct device *gLvds_hawkm_device=NULL;
static struct class *gLvds_hawkm_class=NULL;
static struct mutex gData_mutex;

static void __iomem	*gReg_lvds_ctrl=NULL;
static void __iomem	*gReg_tmg=NULL;

static int gfLvds_transfer=TRUE;


static int lvds_hawkm_open(struct  inode * inode, struct file * file)
{
	//printk("-- lvds_hawkm_open --\n");
	return 0;
}


static int lvds_hawkm_release(struct inode *inode, struct file *file)
{
	//printk("-- lvds_hawkm_release --\n");
	return 0;
}


long lvds_hawkm_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{	
	//printk("-- lvds_hawkm_ioctl --\n");
	return 0;
}

static ssize_t lvds_hawkm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	//printk("-- lvds_hawkm_read --\n");
	return 0;
}


static ssize_t lvds_hawkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	//printk("-- lvds_hawkm_write --\n");
	return 0;
}


static loff_t lvds_hawkm_llseek(struct file *filp, loff_t off, int shence)
{
	//printk("-- lvds_hawkm_llseek --\n");
	return 0;
}


unsigned int lvds_hawkm_poll(struct file *filp, poll_table *wait)
{
	//printk("-- lvds_hawkm_poll --\n");
	return 0;
}


struct file_operations gLvds_hawkm_fops = {
	.owner	= THIS_MODULE,
	.llseek	= lvds_hawkm_llseek,
	.read	= lvds_hawkm_read,
	.write	= lvds_hawkm_write,
	.open	= lvds_hawkm_open,
	.release	= lvds_hawkm_release,
	.unlocked_ioctl = lvds_hawkm_ioctl,
	.poll		= lvds_hawkm_poll,
	//.mmap	= dispenser_mmap,
};

/* sysfs */

static ssize_t lvds_hawkm_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int n;

	mutex_lock(&gData_mutex);

	if(gfLvds_transfer == TRUE) {
		n = sprintf(buf, "1");
	}
	else {
		n = sprintf(buf, "0");
	}

	mutex_unlock(&gData_mutex);
	return n;
}

static ssize_t lvds_hawkm_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t n)
{
	int mode;

	mutex_lock(&gData_mutex);

	sscanf(buf, "%d", &mode);

	if(mode == 0){
		gfLvds_transfer = FALSE;
		writel((unsigned int)0x0, gReg_tmg); // disable Clock
		writel((unsigned int)0x0, gReg_lvds_ctrl); // set output-voltage to LOW
	}
	else if(mode == 1){
		gfLvds_transfer = TRUE;
		writel((unsigned int)0x400040, gReg_lvds_ctrl); // set output-voltage to HIGH
		writel((unsigned int)0x10, gReg_tmg); // enable Clock
	}
	else{
		// do nothing
	}

	mutex_unlock(&gData_mutex);

	return n;
}

static struct device_attribute lvds_hawkm_dev_attrs[] = {
	__ATTR(lvds_hawkm_ctrl, 0666, lvds_hawkm_ctrl_show, lvds_hawkm_ctrl_store),
	__ATTR_NULL
};

/* sysfs end */ 


static int lvds_hawkm_init(void)
{
	int ret=0; 
	gReg_lvds_ctrl = ioremap(REG_LVDS_CTRL,0x4);
	gReg_tmg = ioremap(REG_TMG,0x4);
	
	set_gpio_level(GPIO_LCD_EN, 1); // LCD power-on
	msleep(10);
	writel((unsigned int)0x400040, gReg_lvds_ctrl); // set output-voltage to HIGH
	writel((unsigned int)0x10, gReg_tmg); // enable Clock

	__mutex_init(&gData_mutex,0,0);

	/* 디바이스 주 번호에 대해 동적할당 요청 */
	ret = alloc_chrdev_region(&gDev_major_num, 0, 1, DEVICE_NAME);
	if(ret < 0) {
		printk("[%s][%d] \"alloc_chrdev_region\" Fail..\n", __func__, __LINE__);
		goto fail;
	}

	/* cdev에 파일 연산을 연결한다. */
	cdev_init(&gLvds_hawkm_cdev, &gLvds_hawkm_fops);
	gLvds_hawkm_cdev.owner = THIS_MODULE;
	
	/* cdev에 주/부 번호를 연결한다. */
	ret = cdev_add(&gLvds_hawkm_cdev, gDev_major_num, 1);
	if(ret) {
		printk("[%s][%d] \"cdev_add\" Fail..\n", __func__, __LINE__);
		goto fail_cdev_add;
	}

	/* sysfs 항목 설정 */
	gLvds_hawkm_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(gLvds_hawkm_class)) 
	{
		printk("[%s][%d] \"class_create\" Fail..\n", __func__, __LINE__);
		goto fail_class_create;
	}

	/* USE_SYSFS */
	gLvds_hawkm_class->dev_attrs = lvds_hawkm_dev_attrs;

	/* uevents를 udev로 보내 /dev 노드를 생성한다. */
	gLvds_hawkm_device = device_create(gLvds_hawkm_class, NULL, gDev_major_num, NULL, DEVICE_NAME);
	if(IS_ERR(gLvds_hawkm_device)) {
		goto fail_device_create;
	}

	return 0;

fail_device_create:
	class_destroy(gLvds_hawkm_class);
fail_class_create:
	cdev_del(&gLvds_hawkm_cdev);
fail_cdev_add:
//fail_alloc_chrdev_region: 	//hsguy.son (modify)
fail:

	return -EINVAL;
}

static void lvds_hawkm_exit(void)
{
	mutex_destroy(&gData_mutex);

	if (gLvds_hawkm_device)
	{
		device_destroy(gLvds_hawkm_class, gDev_major_num);
	}
	if (gLvds_hawkm_class)
	{
		class_destroy(gLvds_hawkm_class);
	}
	cdev_del(&gLvds_hawkm_cdev);
}

module_init(lvds_hawkm_init);
module_exit(lvds_hawkm_exit);

MODULE_LICENSE("GPL");
