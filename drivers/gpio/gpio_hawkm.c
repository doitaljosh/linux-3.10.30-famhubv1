#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/of_device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/io.h>

#define GPIO_PASS_MODE_SET 	-1
#define GPIO_OPMODE_IN 		1
#define GPIO_OPMODE_OUT 		2
#define GPIO_H 1
#define GPIO_L 0

#define NAME_SIZE	30
#define SIZE_GPIO_INIT_TABLE	(sizeof(gpio_init_table) / sizeof(gpio_init_table[0]))

struct gpio_table {
	unsigned int num;
	int	mode;
	int	def;
	char name[NAME_SIZE];
};


#define	GPIO_ST_EN					0x0004	// P0.4
#define	GPIO_BT_EN					0x0102	// P1.2
#define	GPIO_LCD_EN					0x0103	// P1.3
#define	GPIO_TOUCH_EN				0x0104	// P1.4
#define	GPIO_BL_12V_EN				0x0105	// P1.5
#define	GPIO_AMP_RESET				0x0107	// P1.7
#define	GPIO_USB_DEBUG_EN			0x0200	// P2.0
#define	GPIO_AUDIO12V_EN			0x0202	// P2.2
#define	GPIO_CAM1_EN				0x0302	// P3.2
#define	GPIO_CODEC_EN				0x0306	// P3.6
#define	GPIO_BL_EN					0x0b04	// P11.4
#define	GPIO_WIFI_EN				0x0b05	// P11.5
#define	GPIO_TCHKEY_BL0_MIC_OFF_EN	0x0c02	// P12.2
#define	GPIO_TCHKEY_BL1_MIC_ON_EN	0x0c04	// P12.4
#define	GPIO_TCHKEY_BL2_ALL_EN		0x0d01	// P13.1

static const struct gpio_table gpio_init_table[] = {
	{
		.num = GPIO_ST_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_H,
		.name = "st_en",
	},
	{
		.num = GPIO_BT_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_H,
		.name = "bt_en",
	},
	{
		.num = GPIO_LCD_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "lcd_en",
	},
	{
		.num = GPIO_TOUCH_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "touch_en",
	},
	{
		.num = GPIO_BL_12V_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "bl_12v_en",
	},
	{
		.num = GPIO_AMP_RESET,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "amp_reset",
	},
	{
		.num = GPIO_USB_DEBUG_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "usb_debug_en",
	},
	{
		.num = GPIO_AUDIO12V_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_H,
		.name = "audio12v_en",
	},
	{
		.num = GPIO_CAM1_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "cam1_en",
	},
	{
		.num = GPIO_CODEC_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "codec_en",
	},
	{
		.num = GPIO_BL_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "bl_en",
	},
	{
		.num = GPIO_WIFI_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "wifi_en",
	},
	{
		.num = GPIO_TCHKEY_BL0_MIC_OFF_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "tchkey_bl0_mic_off_en",
	},
	{
		.num = GPIO_TCHKEY_BL1_MIC_ON_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "tchkey_bl1_mic_on_en",
	},
	{
		.num = GPIO_TCHKEY_BL2_ALL_EN,
		.mode = GPIO_OPMODE_OUT,
		.def = GPIO_L,
		.name = "tchkey_bl2_all_en",
	},
};

typedef struct
{
	unsigned int cmd; // 0=Read, 1=Write
	unsigned int gpio_num; // 0~19 
	unsigned int val; // output-value (This is valid when the cmd is Write)
}_ioctl_cmd;

#define	DEVICE_NAME   		"gpio_hawkm"
#define IOCTL_MAGIC			'J'
#define IOCTL_GENERIC		_IOWR(IOCTL_MAGIC, 0, _ioctl_cmd)

static dev_t gDev_major_num=0;
static struct cdev gGpio_hawkm_cdev;
static struct device *gGpio_hawkm_device=NULL;
static struct class *gGpio_hawkm_class=NULL;
static struct mutex gData_mutex;
static unsigned int gpio_port=0;

/*
static int get_gpio_table_index(unsigned int num)
{
	int i;

	for (i = 0; i < SIZE_GPIO_INIT_TABLE; i++) {
		if (gpio_init_table[i].num == num)
			return i;
	}
		
	return -1;
}
*/

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
	/*printk("port %d, index %d, gpio %d\n", port, index, *gpio);*/
	
	return 0;
}

int gpio_config(unsigned int gpio, unsigned int mode, char * name)
{
	unsigned int gpio_num = 0;
	int ret = 0;
	
	unsigned int gpioPort = 0x3ff & (gpio>>8);
    	unsigned int gpioIndex = 0xff & gpio;

	if (get_gpio_num(gpioPort, gpioIndex, &gpio_num)){
		printk("Can not get GPIO number.\n");
		return -1;
	}
	
	if (!gpio_is_valid(gpio_num)) {
		printk("Invalid GPIO port.\n");
		return -1;
	}
		
	ret = gpio_request(gpio_num, name);
	if (ret) {
		if (ret != -EBUSY)
			printk("Can't request gpio_num: %d\n", ret);
		return -1;
	}

	switch(mode)
	{
		case GPIO_PASS_MODE_SET:
			break;
		case GPIO_OPMODE_IN:
			gpio_direction_input(gpio_num); 
			break;
		case GPIO_OPMODE_OUT:
			gpio_direction_output(gpio_num, 1);
			break;
		default:
			return -1;
	}
	
	return 0;
}

int set_gpio_level(int gpio,int level)
{	
	unsigned int gpio_num = 0;
    unsigned int gpioPort = 0x3ff & (gpio>>8);
    unsigned int gpioIndex = 0xff & gpio;
	
	if (get_gpio_num(gpioPort, gpioIndex, &gpio_num)){
		printk("Can not get GPIO number.\n");
		return -1;
	}
	
	if (!gpio_is_valid(gpio_num)) {
		printk("Invalid GPIO port.\n");
		return -1;
	}
	gpio_set_value(gpio_num,level);
	
	return 0;
}

unsigned int get_gpio_level(int gpio)
{
	unsigned int value = 0;
	unsigned int gpio_num = 0;
	unsigned int gpioPort = 0x3ff & (gpio>>8);
	unsigned int gpioIndex = 0xff & gpio;
	if (get_gpio_num(gpioPort, gpioIndex, &gpio_num)){
		printk("Can not get GPIO number.\n");
		return -1;
	}
	if (!gpio_is_valid(gpio_num)) {
		printk("Invalid GPIO port.\n");
		return -1;
	}
	value = gpio_get_value_cansleep(gpio_num);	
	return value;
}

int free_gpio(int gpio)
{
	unsigned int gpio_num = 0;
	unsigned int gpioPort = 0x3ff & (gpio>>8);
	unsigned int gpioIndex = 0xff & gpio;
	if (get_gpio_num(gpioPort, gpioIndex, &gpio_num)){
		printk("Can not get GPIO number.\n");
		return -1;
	}
	if (!gpio_is_valid(gpio_num)) {
		printk("Invalid GPIO port.\n");
		return -1;
	}
	gpio_free(gpio_num);
	
	return 0;
}


static void __iomem * REG_PAD_CTRL16; 
static int gpio_hawkm_open(struct  inode * inode, struct file * file)
{
	//printk("-- gpio_hawkm_open --\n");
	return 0;
}


static int gpio_hawkm_release(struct inode *inode, struct file *file)
{
	//printk("-- gpio_hawkm_release --\n");
	return 0;
}


long gpio_hawkm_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{	
	int ret=0;
#if 0	
	_ioctl_cmd ioctl_cmd;
	int size = _IOC_SIZE(cmd);

	mutex_lock(&gData_mutex); // lock

	if(_IOC_TYPE(cmd) != IOCTL_MAGIC)
	{
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}
	switch(cmd)
	{
	case IOCTL_GENERIC:
		copy_from_user((void*)&ioctl_cmd, (const void*)arg, size);

		/* command-validity check */
		if((ioctl_cmd.cmd<0) || (ioctl_cmd.cmd>1)) {
			printk("[%s][%d] invalid cmd\n", __func__, __LINE__);
			mutex_unlock(&gData_mutex); // unlock
			return -EINVAL;
		}
		if((ioctl_cmd.gpio_num<0) || (ioctl_cmd.gpio_num>19)) {
			printk("[%s][%d] invalid gpio_num\n", __func__, __LINE__);
			mutex_unlock(&gData_mutex); // unlock
			return -EINVAL;
		}
		if((ioctl_cmd.val<0) || (ioctl_cmd.val>1)) {
			printk("[%s][%d] invalid gpio_num\n", __func__, __LINE__);
			mutex_unlock(&gData_mutex); // unlock
			return -EINVAL;
		}

		if(gGpio_init_table[ioctl_cmd.gpio_num]==-1) {
			printk("[%s][%d] not-used gpio\n", __func__, __LINE__);
			mutex_unlock(&gData_mutex); // unlock
			return -EINVAL;
		}
	
		/* do command */
		if(ioctl_cmd.cmd == 0) // READ
		{
			ret=gpio_get_value(ioctl_cmd.gpio_num+92); // return current output-value
		}
		else if(ioctl_cmd.cmd == 1) // WRITE
		{
			gpio_set_value(ioctl_cmd.gpio_num+92, ioctl_cmd.val);
			ret=0; // always return success
		}
		break;
	default:
		printk("[%s][%d] un-known ioctl-cmd\n", __func__, __LINE__);
		ret=-EINVAL;
		break;
	}

	mutex_unlock(&gData_mutex); // unlock
	
#endif
	return ret;
}

/* add for gpio init */ 

static inline void writel_convert(int val, int mask, unsigned int *addr)
{
	unsigned int tmp;

	tmp = readl(addr);

	if(val == 1)
	{
		writel(tmp | (1 << mask), addr);
	}
	else
	{
		writel(tmp & (~(1 << mask)), addr);
	}
}

static void __gpio_request(bool mode_set)
{
	int i;
	int ret=0;

	for (i = 0; i < SIZE_GPIO_INIT_TABLE; i++) {
		if (mode_set == true)
			ret = gpio_config(gpio_init_table[i].num, gpio_init_table[i].mode, (char*)gpio_init_table[i].name);
		else
			ret = gpio_config(gpio_init_table[i].num, GPIO_PASS_MODE_SET, (char*)gpio_init_table[i].name);

		if(ret) {
			printk("[%s][%d] gpio_request_one Fail.. (%s)\n", __func__, __LINE__, gpio_init_table[i].name);
		}	
	} //for
}

static void __gpio_init(void)
{
	int i;

	for (i = 0; i < SIZE_GPIO_INIT_TABLE; i++) {
		set_gpio_level(gpio_init_table[i].num, gpio_init_table[i].def);
	} //for
}

static void __gpio_free(void)
{
	int i;
	int ret=0;

	for (i = 0; i < SIZE_GPIO_INIT_TABLE; i++) {
		ret = free_gpio(gpio_init_table[i].num);

		if(ret) {
			printk("[%s][%d] gpio_request_one Fail.. (%s)\n", __func__, __LINE__, gpio_init_table[i].name);
		}		
	} //for
}

static void __cpu_gpio_init(void)
{

	REG_PAD_CTRL16 = ioremap(0x005C1040, sizeof(u32));
	
	/* QPI disable for b_GPIO1*/
	writel_convert(0, 0, REG_PAD_CTRL16);

	/* i2c9 disable for b_GPIO2, b_GPIO3 */
	writel_convert(0, 23, REG_PAD_CTRL16);

	/* i2c11 disable for b_GPIO13, b_GPIO14 */
	writel_convert(0, 25, REG_PAD_CTRL16);

	iounmap(REG_PAD_CTRL16);
}

/* end */

static ssize_t gpio_hawkm_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	//printk("-- gpio_hawkm_read --\n");
	return 0;
}


static ssize_t gpio_hawkm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	//printk("-- gpio_hawkm_write --\n");
	return 0;
}


static loff_t gpio_hawkm_llseek(struct file *filp, loff_t off, int shence)
{
	//printk("-- gpio_hawkm_llseek --\n");
	return 0;
}


unsigned int gpio_hawkm_poll(struct file *filp, poll_table *wait)
{
	//printk("-- gpio_hawkm_poll --\n");
	return 0;
}


struct file_operations gGpio_hawkm_fops = {
	.owner	= THIS_MODULE,
	.llseek	= gpio_hawkm_llseek,
	.read	= gpio_hawkm_read,
	.write	= gpio_hawkm_write,
	.open	= gpio_hawkm_open,
	.release	= gpio_hawkm_release,
	.unlocked_ioctl = gpio_hawkm_ioctl,
	.poll		= gpio_hawkm_poll,
	//.mmap	= dispenser_mmap,
};

/* sysfs */

static ssize_t show_gpio_list(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	size_t size = 0;
	int i;
	unsigned int value;

	mutex_lock(&gData_mutex); // lock
	
	__gpio_request(false);

	for (i = 0; i < SIZE_GPIO_INIT_TABLE; i++) {
		value = get_gpio_level(gpio_init_table[i].num);
		size += sprintf(buf+size,"%d :\t%s ( %04x ) : %d\n", i, gpio_init_table[i].name, gpio_init_table[i].num, value);
	}
	
	__gpio_free();

	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_gpio_value(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock
/*
	index = get_gpio_table_index(gpio_port);
	if (index < 0)	{
		printk("[%s][%d] find index Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex);
		return -ENODEV;
	}
*/
	gpio_config(gpio_port, GPIO_PASS_MODE_SET, NULL);

	value = get_gpio_level(gpio_port);
	
	free_gpio(gpio_port);
	
	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 1;
}


static ssize_t store_gpio_value(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return ret;
	}
/*
	index = get_gpio_table_index(gpio_port);
	if (index < 0)	{
		printk("[%s][%d] find index Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex);
		return -ENODEV;
	}
*/
	gpio_config(gpio_port, GPIO_PASS_MODE_SET, NULL);

	set_gpio_level(gpio_port, value);
	
	free_gpio(gpio_port);
	printk(KERN_INFO "set GPIO[0x%04x] = [%lu]\n", gpio_port, value);

	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t store_gpio_port(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	mutex_lock(&gData_mutex); // lock
	
	sscanf(buf, "%x", &gpio_port);

	mutex_unlock(&gData_mutex); // unlock

	return count;
}

static ssize_t show_gpio_port(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	mutex_lock(&gData_mutex); // lock

	sprintf(buf, "%x\n", gpio_port);

	mutex_unlock(&gData_mutex); // unlock

	return 1;
}

static ssize_t show_cam1_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_CAM1_EN, GPIO_PASS_MODE_SET, "cam1_en");

	value = get_gpio_level(GPIO_CAM1_EN);
	
	free_gpio(GPIO_CAM1_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_cam1_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_CAM1_EN, GPIO_PASS_MODE_SET, "cam1_en");

	set_gpio_level(GPIO_CAM1_EN, value);
	
	free_gpio(GPIO_CAM1_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_CAM1_EN, "cam1_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}


static ssize_t show_lcd_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_LCD_EN, GPIO_PASS_MODE_SET, "lcd_en");

	value = get_gpio_level(GPIO_LCD_EN);
	
	free_gpio(GPIO_LCD_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_lcd_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_LCD_EN, GPIO_PASS_MODE_SET, "lcd_en");

	set_gpio_level(GPIO_LCD_EN, value);
	
	free_gpio(GPIO_LCD_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_LCD_EN, "lcd_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_bl_12v_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_BL_12V_EN, GPIO_PASS_MODE_SET, "bl_12v_en");

	value = get_gpio_level(GPIO_BL_12V_EN);
	
	free_gpio(GPIO_BL_12V_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_bl_12v_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_BL_12V_EN, GPIO_PASS_MODE_SET, "bl_12v_en");

	set_gpio_level(GPIO_BL_12V_EN, value);
	
	free_gpio(GPIO_BL_12V_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_BL_12V_EN, "bl_12v_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_bl_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_BL_EN, GPIO_PASS_MODE_SET, "bl_en");

	value = get_gpio_level(GPIO_BL_EN);
	
	free_gpio(GPIO_BL_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_bl_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_BL_EN, GPIO_PASS_MODE_SET, "bl_12v_en");

	set_gpio_level(GPIO_BL_EN, value);
	
	free_gpio(GPIO_BL_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_BL_EN, "bl_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_tchkey_bl0_mic_off_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_TCHKEY_BL0_MIC_OFF_EN, GPIO_PASS_MODE_SET, "tchkey_bl0_mic_off_en");

	value = get_gpio_level(GPIO_TCHKEY_BL0_MIC_OFF_EN);
	
	free_gpio(GPIO_TCHKEY_BL0_MIC_OFF_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_tchkey_bl0_mic_off_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_TCHKEY_BL0_MIC_OFF_EN, GPIO_PASS_MODE_SET, "tchkey_bl0_mic_off_en");

	set_gpio_level(GPIO_TCHKEY_BL0_MIC_OFF_EN, value);
	
	free_gpio(GPIO_TCHKEY_BL0_MIC_OFF_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_TCHKEY_BL0_MIC_OFF_EN, "tchkey_bl0_mic_off_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_tchkey_bl1_mic_on_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_TCHKEY_BL1_MIC_ON_EN, GPIO_PASS_MODE_SET, "tchkey_bl1_mic_on_en");

	value = get_gpio_level(GPIO_TCHKEY_BL1_MIC_ON_EN);
	
	free_gpio(GPIO_TCHKEY_BL1_MIC_ON_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_tchkey_bl1_mic_on_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_TCHKEY_BL1_MIC_ON_EN, GPIO_PASS_MODE_SET, "tchkey_bl1_mic_on_en");

	set_gpio_level(GPIO_TCHKEY_BL1_MIC_ON_EN, value);
	
	free_gpio(GPIO_TCHKEY_BL1_MIC_ON_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_TCHKEY_BL1_MIC_ON_EN, "tchkey_bl1_mic_on_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t show_tchkey_bl2_all_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	gpio_config(GPIO_TCHKEY_BL2_ALL_EN, GPIO_PASS_MODE_SET, "tchkey_bl2_all_en");

	value = get_gpio_level(GPIO_TCHKEY_BL2_ALL_EN);
	
	free_gpio(GPIO_TCHKEY_BL2_ALL_EN);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 2;
}

static ssize_t store_tchkey_bl2_all_en(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(GPIO_TCHKEY_BL2_ALL_EN, GPIO_PASS_MODE_SET, "tchkey_bl2_all_en");

	set_gpio_level(GPIO_TCHKEY_BL2_ALL_EN, value);
	
	free_gpio(GPIO_TCHKEY_BL2_ALL_EN);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", GPIO_TCHKEY_BL2_ALL_EN, "tchkey_bl2_all_en", value);
	mutex_unlock(&gData_mutex); // unlock

	return size;
}

static ssize_t hawkm_gpio_show(struct device *dev, struct device_attribute *attr, char *buf, int nr)
{
	unsigned int value;

	mutex_lock(&gData_mutex); // lock

	if (nr >= SIZE_GPIO_INIT_TABLE) {
		printk("[%s][%d] invalid parameter\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
		
	}

	gpio_config(gpio_init_table[nr].num, GPIO_PASS_MODE_SET, (char*)gpio_init_table[nr].name);

	value = get_gpio_level(gpio_init_table[nr].num);
	
	free_gpio(gpio_init_table[nr].num);

	sprintf(buf,"%d\n", value);
	mutex_unlock(&gData_mutex); // unlock

	return 1;
}

static ssize_t hawkm_gpio_store(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t n,int nr)
{
	int ret = -1;
	unsigned long value;

	mutex_lock(&gData_mutex); // lock

	ret = kstrtoul(buf, 10, &value);
	if(ret < 0) {
		printk("[%s][%d] strict_strtoul Fail..\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	if (nr >= SIZE_GPIO_INIT_TABLE) {
		printk("[%s][%d] invalid parameter\n", __func__, __LINE__);
		mutex_unlock(&gData_mutex); // unlock
		return -EINVAL;
	}

	gpio_config(gpio_init_table[nr].num, GPIO_PASS_MODE_SET, (char*)gpio_init_table[nr].name);

	set_gpio_level(gpio_init_table[nr].num, value);
	
	free_gpio(gpio_init_table[nr].num);

	printk(KERN_INFO "set GPIO[0x%04x][%s] = [%lu]\n", gpio_init_table[nr].num, gpio_init_table[nr].name, value);
	mutex_unlock(&gData_mutex); // unlock

	return n;
}



#define hawkm_gpio(nr)	\
static ssize_t hawkm_gpio_##nr##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{																									\
	return hawkm_gpio_show(dev, attr, buf, nr);														\
}	\
static ssize_t hawkm_gpio_##nr##_store(struct device * dev,		\
		struct device_attribute * attr, const char *buf, size_t len)		\
{																	\
	return hawkm_gpio_store(dev,attr,buf,len,nr);				\
}


hawkm_gpio(0)
hawkm_gpio(1)
hawkm_gpio(2)
hawkm_gpio(3)
hawkm_gpio(4)
hawkm_gpio(5)
hawkm_gpio(6)
hawkm_gpio(7)
hawkm_gpio(8)
hawkm_gpio(9)
hawkm_gpio(10)
hawkm_gpio(11)
hawkm_gpio(12)
hawkm_gpio(13)
hawkm_gpio(14)
hawkm_gpio(15)
hawkm_gpio(16)
hawkm_gpio(17)
hawkm_gpio(18)
hawkm_gpio(19)

static struct device_attribute gpio_hawkm_dev_attrs[] = {
	__ATTR(0, S_IWUSR | S_IRUGO,hawkm_gpio_0_show,hawkm_gpio_0_store),
	__ATTR(1, S_IWUSR | S_IRUGO,hawkm_gpio_1_show,hawkm_gpio_1_store),
	__ATTR(2, S_IWUSR | S_IRUGO,hawkm_gpio_2_show,hawkm_gpio_2_store),
	__ATTR(3, S_IWUSR | S_IRUGO,hawkm_gpio_3_show,hawkm_gpio_3_store),
	__ATTR(4, S_IWUSR | S_IRUGO,hawkm_gpio_4_show,hawkm_gpio_4_store),
	__ATTR(5, S_IWUSR | S_IRUGO,hawkm_gpio_5_show,hawkm_gpio_5_store),
	__ATTR(6, S_IWUSR | S_IRUGO,hawkm_gpio_6_show,hawkm_gpio_6_store),
	__ATTR(7, S_IWUSR | S_IRUGO,hawkm_gpio_7_show,hawkm_gpio_7_store),
	__ATTR(8, S_IWUSR | S_IRUGO,hawkm_gpio_8_show,hawkm_gpio_8_store),
	__ATTR(9, S_IWUSR | S_IRUGO,hawkm_gpio_9_show,hawkm_gpio_9_store),
	__ATTR(10, S_IWUSR | S_IRUGO,hawkm_gpio_10_show,hawkm_gpio_10_store),
	__ATTR(11, S_IWUSR | S_IRUGO,hawkm_gpio_11_show,hawkm_gpio_11_store),
	__ATTR(12, S_IWUSR | S_IRUGO,hawkm_gpio_12_show,hawkm_gpio_12_store),
	__ATTR(13, S_IWUSR | S_IRUGO,hawkm_gpio_13_show,hawkm_gpio_13_store),
	__ATTR(14, S_IWUSR | S_IRUGO,hawkm_gpio_14_show,hawkm_gpio_14_store),
	__ATTR(15, S_IWUSR | S_IRUGO,hawkm_gpio_15_show,hawkm_gpio_15_store),
	__ATTR(16, S_IWUSR | S_IRUGO,hawkm_gpio_16_show,hawkm_gpio_16_store),
	__ATTR(17, S_IWUSR | S_IRUGO,hawkm_gpio_17_show,hawkm_gpio_17_store),
	__ATTR(18, S_IWUSR | S_IRUGO,hawkm_gpio_18_show,hawkm_gpio_18_store),
	__ATTR(19, S_IWUSR | S_IRUGO,hawkm_gpio_19_show,hawkm_gpio_19_store),
	__ATTR(cam1_en, S_IWUGO | S_IRUGO, show_cam1_en, store_cam1_en),
	__ATTR(lcd_en, S_IWUGO | S_IRUGO, show_lcd_en, store_lcd_en),
	__ATTR(bl_12v_en, S_IWUGO | S_IRUGO, show_bl_12v_en, store_bl_12v_en),
	__ATTR(bl_en, S_IWUGO | S_IRUGO, show_bl_en, store_bl_en),
	__ATTR(tchkey_bl0_mic_off_en, S_IWUGO | S_IRUGO, show_tchkey_bl0_mic_off_en, store_tchkey_bl0_mic_off_en),
	__ATTR(tchkey_bl1_mic_on_en, S_IWUGO | S_IRUGO, show_tchkey_bl1_mic_on_en, store_tchkey_bl1_mic_on_en),
	__ATTR(tchkey_bl2_all_en, S_IWUGO | S_IRUGO, show_tchkey_bl2_all_en, store_tchkey_bl2_all_en),
	__ATTR(list, S_IRUGO, show_gpio_list,NULL),
	__ATTR(port, S_IWUSR | S_IRUGO, show_gpio_port,	store_gpio_port),
	__ATTR(value, S_IWUSR | S_IRUGO, show_gpio_value, store_gpio_value),
	__ATTR_NULL
};



/* sysfs end */ 


static int gpio_hawkm_init(void)
{
	int ret=0; 

	__mutex_init(&gData_mutex,0,0);

	__gpio_request(true);

	__gpio_init();

	/* set gpio register */
	__cpu_gpio_init(); 

	ret = alloc_chrdev_region(&gDev_major_num, 0, 1, DEVICE_NAME);
	if(ret < 0) {
		printk("[%s][%d] \"alloc_chrdev_region\" Fail..\n", __func__, __LINE__);
		goto fail;
	}

	/* cdev에 파일 연산을 연결한다. */
	cdev_init(&gGpio_hawkm_cdev, &gGpio_hawkm_fops);
	gGpio_hawkm_cdev.owner = THIS_MODULE;
	

	/* cdev에 주/부 번호를 연결한다. */
	ret = cdev_add(&gGpio_hawkm_cdev, gDev_major_num, 1);
	if(ret) {
		printk("[%s][%d] \"cdev_add\" Fail..\n", __func__, __LINE__);
		goto fail_cdev_add;
	}

	/* sysfs 항목 설정 */
	gGpio_hawkm_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(gGpio_hawkm_class)) 
	{
		printk("[%s][%d] \"class_create\" Fail..\n", __func__, __LINE__);
		goto fail_class_create;
	}


	/* USE_SYSFS */
	gGpio_hawkm_class->dev_attrs = gpio_hawkm_dev_attrs;
	gGpio_hawkm_device = device_create(gGpio_hawkm_class, NULL, gDev_major_num, NULL, "gpios");
	if(IS_ERR(gGpio_hawkm_device)) {
		goto fail_device_create;
	}


	__gpio_free();

	return 0;

fail_device_create:
	class_destroy(gGpio_hawkm_class);
fail_class_create:
	cdev_del(&gGpio_hawkm_cdev);
fail_cdev_add:
//fail_alloc_chrdev_region: 	//hsguy.son (modify)
fail:
	__gpio_free();

	return -EINVAL;
}

static void gpio_hawkm_exit(void)
{
	mutex_destroy(&gData_mutex);

	if (gGpio_hawkm_device)
	{
		device_destroy(gGpio_hawkm_class, gDev_major_num);
	}
	if (gGpio_hawkm_class)
	{
		class_destroy(gGpio_hawkm_class);
	}
	cdev_del(&gGpio_hawkm_cdev);

	__gpio_free();

}

module_init(gpio_hawkm_init);
module_exit(gpio_hawkm_exit);

MODULE_LICENSE("GPL");
