#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/of_device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>

#include <linux/stat.h>
#include <linux/types.h>
#include <linux/fcntl.h>


#define	DEVICE_NAME   		"misc_pm"

#define	GPIO_USB_EN			0x0200 // P2.0
#define	GPIO_WIFI_EN		0x0b05 // P11.5
#define	GPIO_ST_EN			0x0004 // P0.4
#define	GPIO_BT_EN			0x0102 // P1.2

struct proc_dir_entry *misc_root_proc_dir = NULL;
struct proc_dir_entry *usb_proc_file = NULL;
struct proc_dir_entry *wifi_proc_file = NULL;
struct proc_dir_entry *st_proc_file = NULL;
struct proc_dir_entry *bt_proc_file = NULL;

static unsigned int usb_gpio_num = 0;
static unsigned int wifi_gpio_num = 0;
static unsigned int st_gpio_num = 0;
static unsigned int bt_gpio_num = 0;

static struct mutex data_mutex; 

static int get_gpio_num(unsigned int port, unsigned int index, unsigned int *gpio)
{
	unsigned int base,sel;
	
	sel=(port>>8)&0xf;
	if(sel==0x1) base=120; 		 //mp0
	else if (sel==0x2) base=280; //mp1
	else if (sel==0x3) base=440; //us
	else if (sel==0x0) base =0;  //ap
	else 
	{
		printk( "plz check AP(0x0xxxx) MP0(0x1xxxx) MP1(0x2xxxx) US(0x3xxxx) pad \n");
		return -1;
	}
	port=port&0x0ff;
	*gpio=(port*8)+index+base;

	return 0;
}

static ssize_t gpio_usb_pm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	int ret=0;

	mutex_lock(&data_mutex); // lock

	if (!gpio_is_valid(usb_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
		ret = -ENODEV;
		goto finish;
	}
	
	sprintf(buf,"%d\n",gpio_get_value_cansleep(usb_gpio_num));

finish:
	mutex_unlock(&data_mutex); // unlock
	return ret;
}

static ssize_t gpio_usb_pm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{

	unsigned int power=0;
	int ret=0;

	mutex_lock(&data_mutex); // lock

	sscanf(buf, "%d", &power);
	
	if((power!=0) && (power!=1)) {
		printk("[%s[%d] only '0' or '1' value can be used!\n", __func__, __LINE__);
		ret = -EINVAL;
		goto finish;
	}
	
	ret = gpio_request(usb_gpio_num, DEVICE_NAME); // request
	if(ret < 0) {
		if (ret != -EBUSY) {
			printk("[%s][%d] gpio_request fail.. (ret=%d)\n", __func__, __LINE__, ret);
			ret = -EINVAL;
			goto finish;
		}
		else
			printk("[%s][%d] gpio_request -EBUSY!\n", __func__, __LINE__);
	}	

 	if (!gpio_is_valid(usb_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
 		ret = -EINVAL;
		goto gpio_fail;
	}
	
	ret = gpio_direction_output(usb_gpio_num, (int)power);
	if(ret < 0) {
		printk("[%s][%d] gpio_direction_output fail.. (ret=%d)\n", __func__, __LINE__, ret);
		ret = -EINVAL;
		goto gpio_fail;
	}

gpio_fail:
	gpio_free(usb_gpio_num); // free
finish:
	mutex_unlock(&data_mutex); // unlock
	
	if(ret < 0)
		return ret;

	return sizeof(power);	
	//return 1;	
}

static ssize_t gpio_wifi_pm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	int ret=0;

	mutex_lock(&data_mutex); // lock
	if (!gpio_is_valid(wifi_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
		ret = -ENODEV;
		goto finish;
	}
	
	sprintf(buf,"%d\n",gpio_get_value_cansleep(wifi_gpio_num));

finish:
	mutex_unlock(&data_mutex); // unlock

	return 0;
}

static ssize_t gpio_wifi_pm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{

	unsigned int power=0;
	int ret=0;

	mutex_lock(&data_mutex); // lock
	sscanf(buf, "%d", &power);
	
	if((power!=0) && (power!=1)) {
		printk("[%s[%d] only '0' or '1' value can be used!\n", __func__, __LINE__);
		ret = -EINVAL;
		goto finish;
	}

	ret = gpio_request(wifi_gpio_num, DEVICE_NAME); // request
	if(ret < 0) {
		if (ret != -EBUSY) {
			printk("[%s][%d] gpio_request fail.. (ret : %d)\n", __func__, __LINE__, ret);
			ret = -EINVAL;
			goto finish;
		}
		else
			printk("[%s][%d] gpio_request -EBUSY!\n", __func__, __LINE__);
	}	

 	if (!gpio_is_valid(wifi_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
 		ret = -EINVAL;
		goto gpio_fail;
	}
	
	ret = gpio_direction_output(wifi_gpio_num, (int)power);
	if(ret < 0) {
		printk("[%s][%d] gpio_direction_output fail.. (ret : %d)\n", __func__, __LINE__, ret);
		ret = -EINVAL;
		goto gpio_fail;
	}

gpio_fail:
	gpio_free(wifi_gpio_num); // free
finish:
	mutex_unlock(&data_mutex); // unlock

	if(ret < 0)
		return ret;

	return sizeof(power);	
//	return 1;	

}

static ssize_t gpio_st_pm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if (!gpio_is_valid(st_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
		return -ENODEV;
	}
	
	sprintf(buf,"%d\n",gpio_get_value_cansleep(st_gpio_num));

	return 0;
}

static ssize_t gpio_st_pm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{

	unsigned int power=0;
	int ret=0;

	mutex_lock(&data_mutex); // lock
	
	sscanf(buf, "%d", &power);
	
	if((power!=0) && (power!=1)) {
		printk("[%s[%d] only '0' or '1' value can be used!\n", __func__, __LINE__);
		ret = -EINVAL;
		goto finish;
	}

	ret = gpio_request(st_gpio_num, DEVICE_NAME); // request
	if(ret < 0) {
		if (ret != -EBUSY) {
			printk("[%s][%d] gpio_request fail.. (ret : %d)\n", __func__, __LINE__, ret);
			ret = -EINVAL;
			goto finish;
		}
		else
			printk("[%s][%d] gpio_request -EBUSY!\n", __func__, __LINE__);
	}	

 	if (!gpio_is_valid(st_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
 		ret = -EINVAL;
		goto gpio_fail;
	}
	
	ret = gpio_direction_output(st_gpio_num, (int)power);
	if(ret < 0) {
		printk("[%s][%d] gpio_direction_output fail.. (ret : %d)\n", __func__, __LINE__, ret);
		ret = -EINVAL;
		goto gpio_fail;
	}

gpio_fail:
	gpio_free(wifi_gpio_num); // free
finish:
	mutex_unlock(&data_mutex); // unlock

	if(ret < 0)
		return ret;

	return sizeof(power);	
}

static ssize_t gpio_bt_pm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if (!gpio_is_valid(bt_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
		return -ENODEV;
	}

	sprintf(buf,"%d\n",gpio_get_value_cansleep(bt_gpio_num));

	return 0;
}

static ssize_t gpio_bt_pm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{

	unsigned int power=0;
	int ret=0;

	mutex_lock(&data_mutex); // lock

	sscanf(buf, "%d", &power);
	
	if((power!=0) && (power!=1)) {
		printk("[%s[%d] only '0' or '1' value can be used!\n", __func__, __LINE__);
		ret = -EINVAL;
		goto finish;
	}

	ret = gpio_request(bt_gpio_num, DEVICE_NAME);
	if(ret < 0) {
		if (ret != -EBUSY) {
			printk("[%s][%d] gpio_request fail.. (ret : %d)\n", __func__, __LINE__, ret);
			ret = -EINVAL;
			goto finish;
		}
		else
			printk("[%s][%d] gpio_request -EBUSY!\n", __func__, __LINE__);
	}	

 	if (!gpio_is_valid(bt_gpio_num)) {
		printk("[%s][%d] gpio_is_valid fail..\n", __func__, __LINE__);
 		ret = -EINVAL;
		goto gpio_fail;
	}
	
	ret = gpio_direction_output(bt_gpio_num, (int)power);
	if(ret < 0) {
		printk("[%s][%d] gpio_direction_output fail.. (ret : %d)\n", __func__, __LINE__, ret);
		ret = -EINVAL;
		goto gpio_fail;
	}

gpio_fail:
	gpio_free(wifi_gpio_num); // free
finish:
	mutex_unlock(&data_mutex); // unlock

	if(ret < 0)
		return ret;
	
	return sizeof(power);	
}

struct file_operations usb_pm_fops = {
	.read	= gpio_usb_pm_read,
	.write	= gpio_usb_pm_write,
};

struct file_operations wifi_pm_fops = {
	.read	= gpio_wifi_pm_read,
	.write	= gpio_wifi_pm_write,
};

struct file_operations st_pm_fops = {
	.read	= gpio_st_pm_read,
	.write	= gpio_st_pm_write,
};

struct file_operations bt_pm_fops = {
	.read	= gpio_bt_pm_read,
	.write	= gpio_bt_pm_write,
};

static int misc_procfs_attach(void)
{
	char buf[] = "driver/misc-pm";

	misc_root_proc_dir = proc_mkdir(buf, NULL);

	if(misc_root_proc_dir == NULL){
		printk("[%s][%d] proc_mkdir Fail.. \n",__func__,__LINE__);
		return -1;
	}

	usb_proc_file = proc_create("usb-power", 0666, misc_root_proc_dir, &usb_pm_fops);
	if(usb_proc_file == NULL){
		printk("[%s][%d] proc_create Fail.. \n",__func__,__LINE__);
		return -1;
	}

	wifi_proc_file = proc_create("wifi-power", 0666, misc_root_proc_dir, &wifi_pm_fops);
	if(wifi_proc_file == NULL){
		printk("[%s][%d] proc_create Fail.. \n",__func__,__LINE__);
		return -1;
	}

	st_proc_file = proc_create("st-power", 0666, misc_root_proc_dir, &st_pm_fops);
	if(st_proc_file == NULL){
		printk("[%s][%d] proc_create Fail.. \n",__func__,__LINE__);
		return -1;
	}

	bt_proc_file = proc_create("bt-power", 0666, misc_root_proc_dir, &bt_pm_fops);
	if(bt_proc_file == NULL){
		printk("[%s][%d] proc_create Fail.. \n",__func__,__LINE__);
		return -1;
	}

	return 0;
}

static inline void misc_procfs_remove(void)
{
	char buf[] = "driver/misc-pm";

	remove_proc_entry("usb-power", usb_proc_file);
	remove_proc_entry("wifi-power", wifi_proc_file);
	remove_proc_entry("st-power", st_proc_file);
	remove_proc_entry("bt-power", bt_proc_file);

	remove_proc_entry(buf, NULL);
}

static int misc_pm_probe(struct platform_device *pdev)
{
//	int ret=-1;

	__mutex_init(&data_mutex,0,0); 

	printk("\n\n-- [%s][%d][JOON] misc_pm_probe --\n\n",__func__,__LINE__); // JOON_TEST

	if (get_gpio_num((GPIO_USB_EN>>8) & 0xFF, (GPIO_USB_EN & 0xFF), &usb_gpio_num)) {
		printk("[%s][%d] get_gpio_num Fail..\n", __func__, __LINE__);
		return -1;
	}

	if (get_gpio_num((GPIO_WIFI_EN>>8) & 0xFF, (GPIO_WIFI_EN & 0xFF), &wifi_gpio_num)) {
		printk("[%s][%d] get_gpio_num Fail..\n", __func__, __LINE__);
		return -1;
	}

	if (get_gpio_num((GPIO_ST_EN>>8) & 0xFF, (GPIO_ST_EN & 0xFF), &st_gpio_num)) {
		printk("[%s][%d] get_gpio_num Fail..\n", __func__, __LINE__);
		return -1;
	}
	if (get_gpio_num((GPIO_BT_EN>>8) & 0xFF, (GPIO_BT_EN & 0xFF), &bt_gpio_num)) {
		printk("[%s][%d] get_gpio_num Fail..\n", __func__, __LINE__);
		return -1;
	}

	if(misc_procfs_attach() < 0){
		printk("[%s][%d] procfs_attach Fail..\n", __func__, __LINE__);
		goto fail_procfs_attach;
	}
	
	printk("[%s][%d] misc_pm_probe Success\n", __func__, __LINE__);
	return 0;

fail_procfs_attach:
	misc_procfs_remove();
	printk("[%s][%d] misc_pm_probe Fail..\n", __func__, __LINE__);

	return -EPERM;
}

static int misc_pm_remove(struct platform_device *pdev)
{
	mutex_destroy(&data_mutex);
	misc_procfs_remove();
	printk("misc gpio is released !!\n");
	return 0;
}

static struct platform_device misc_pm_dev = {
	.name           = "misc_pm",
};

static struct platform_driver misc_pm_driver = {
	.driver.name    = "misc_pm",
	.driver.owner   = THIS_MODULE,
	.probe          = misc_pm_probe,
	.remove         = misc_pm_remove,
};

static int misc_pm_init(void)
{
	
	platform_device_register(&misc_pm_dev);
	return platform_driver_register(&misc_pm_driver);
}

static void misc_pm_exit(void)
{

	platform_driver_unregister(&misc_pm_driver);

}

module_init(misc_pm_init);
module_exit(misc_pm_exit);

MODULE_LICENSE("GPL");
