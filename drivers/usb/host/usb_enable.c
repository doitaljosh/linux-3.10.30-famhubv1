#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/cdev.h>

#include "ioctl_usben.h"
#include <linux/mfd/sdp_micom.h>
#include <mach/soc.h>
#include <linux/priority_devconfig.h>


static int gpio;
static struct cdev usben_gpio_cdev;
static int current_gpio_val;
static dev_t usben_id;
static int *read_status;

enum usben_sel{
	RESET_HAWKM_USER_HIGH,
	RESET_HAWKM_USER_LOW,
	RESET_BT,
	RESET_WIFI,
	RESET_MOIP,					/* For internal MoIP, S9500 */
	SET_BT_LOW,
	SET_WIFI_LOW,
	RESET_TV_B_MOIP,			/* For external MoIP, S9000 */
	SET_TV_B_MOIP_LOW,			/* For external MoIP, S9000 */
	SET_TV_B_MOIP_HIGH,			/* For external MoIP, S9000 */
	SET_INTERNAL_MOIP_HIGH,		/* For internal MoIP, S9500 */
	SET_INTERNAL_MOIP_LOW,		/* For internal MoIP, S9500 */
};

#ifdef CONFIG_ARCH_SDP1404
extern int get_tv_chip_data(char *fn_str);
#endif

#ifdef CONFIG_ARCH_SDP1406
#define BOSTON_USB_ENABLE		6
#define BOSTON_USB_SET_HIGH		1
#define BOSTON_USB_SET_LOW		0
#define BOSTON_PLUG				1
#define BOSTON_UNPLUG			0
#define BOSTON_MAX_CTRL_TRY		10

extern int tztv_hdmi_isOCMConnected;
extern int Boston_Gpio_Set(int portnumber, bool onoff);
#endif

struct sdp_micom_msg micom_msg[] = {
		[RESET_HAWKM_USER_HIGH] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0x38,
				.msg[1]		= 2,
				.msg[2] 	= 1,
		},
		[RESET_HAWKM_USER_LOW] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0x38,
				.msg[1]		= 2,
				.msg[2]		= 0,
		},
		[RESET_BT] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 0,
		},
		[RESET_WIFI] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 2,
		},
		[RESET_MOIP] = {								/* For internal MoIP, S9500 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 4,
		},
		[SET_BT_LOW] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 5,
		},
		[SET_WIFI_LOW] = {
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 6,
		},
		[RESET_TV_B_MOIP] = {							/* For external MoIP, S9000 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length		= KEY_PACKET_DATA_SIZE,
				.msg[0]		= 0xB3,
				.msg[1]		= 7,
		},
		[SET_TV_B_MOIP_LOW] = {							/* For external MoIP, S9000 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length 	= KEY_PACKET_DATA_SIZE,
				.msg[0] 	= 0xB3,
				.msg[1] 	= 8,
		},
		[SET_TV_B_MOIP_HIGH] = {						/* For external MoIP, S9000 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length 	= KEY_PACKET_DATA_SIZE,
				.msg[0] 	= 0xB3,
				.msg[1] 	= 9,
		},
		[SET_INTERNAL_MOIP_HIGH] = {					/* For internal MoIP, S9500 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length 	= KEY_PACKET_DATA_SIZE,
				.msg[0] 	= 0xB3,
				.msg[1] 	= 12,
		},
		[SET_INTERNAL_MOIP_LOW] = {						/* For internal MoIP, S9500 */
				.msg_type	= MICOM_NORMAL_DATA,
				.length 	= KEY_PACKET_DATA_SIZE,
				.msg[0] 	= 0xB3,
				.msg[1] 	= 13,
		},
};

int usben_reset(int value)
{
	char cmd = 0, ack = 0, data[2] = {0, 0};
	int len = 0, ret = 0, cnt = 0;

	cmd = micom_msg[value].msg[0];
	ack = micom_msg[value].msg[0];
	if( cmd == 0xB3 )
	{
		data[0] = micom_msg[value].msg[1];
		len = 1;
	}
	else if( cmd == 0x38 )
	{
		data[0] = micom_msg[value].msg[1];
		data[1] = micom_msg[value].msg[2];
		len = 2;
	}

	do
	{
		ret = sdp_micom_send_cmd_ack(cmd, ack, data, len);
		if( ret )
		{
			printk( KERN_ERR "%s : Send msg fail(value=%d, cnt=%d)\n", __func__, value, cnt );
			msleep( 100 );
		}
		cnt++;
	} while( ret && (cnt < 3) );

	return 0;
}
EXPORT_SYMBOL_GPL(usben_reset);

#ifdef CONFIG_ARCH_SDP1404
int tv_is_golfp_9k( void )
{
	enum sdp_sys_info tv_side_main_chip;

	tv_side_main_chip = get_tv_chip_data( "tztv_config_chip_type_tv" );
	if( tv_side_main_chip == SYSTEM_INFO_GOLF_UHD_14_TV_SIDE )
	{
		return true;
	}

	return false;
}
#endif

int usben_ioctl( struct file *file,		/* ditto */
				unsigned int ioctl_num,	/* number and param for ioctl */
				unsigned long ioctl_param )
{
#ifdef CONFIG_ARCH_SDP1406
	int ret = 0, cnt = 0;
#endif

	switch ( ioctl_num ) {
	case IOCTL_USBEN_SET:
		if( ioctl_param != 0 && ioctl_param != 1 )
		{
			printk( "Wrong msg to set GPIO\n" );
			return -1;
		}

		current_gpio_val = ioctl_param;

		if( ioctl_param )		/* set gpio for user port to high */
		{
			if( soc_is_sdp1406( ) )
			{
				usben_reset(RESET_HAWKM_USER_HIGH);
				printk( KERN_ERR "3G Dongle Port set to 1\n" );
				msleep( 1 );

#ifdef CONFIG_ARCH_SDP1406
				if( soc_is_sdp1406uhd( ) && (tztv_hdmi_isOCMConnected == BOSTON_PLUG) )
				{
					do
					{
						ret = Boston_Gpio_Set( BOSTON_USB_ENABLE, BOSTON_USB_SET_HIGH );
						msleep( 500 );
					} while( ret && ((++cnt) <= BOSTON_MAX_CTRL_TRY) );
					printk( KERN_ERR "Boston GPIO set to 1 - [%d][%d]\n", ret, cnt );
				}
#endif
			}
			if( soc_is_sdp1404( ) )
			{
				usben_reset(SET_TV_B_MOIP_HIGH);		// USB_ENABLE
				msleep( 100 );
				usben_reset(SET_INTERNAL_MOIP_HIGH);	// AIT_RESET
#ifdef CONFIG_ARCH_SDP1404
				if( tv_is_golfp_9k( ) )
				{
					usben_reset(RESET_MOIP);
					printk( KERN_ERR "is connected with golf.p 9k\n" );
				}
#endif
				printk( KERN_ERR "TV board MOIP Port set to 1\n" );
			}

			gpio_set_value( (u32)gpio, 1 );
			printk( KERN_ERR "USB Enabled GPIO set to 1\n" );
			msleep( 1 );
		}
		else if( !ioctl_param )	/* set gpio for user port to low */
		{
			if( soc_is_sdp1406( ) )
			{
				usben_reset(RESET_HAWKM_USER_LOW);
				printk( KERN_ERR "3G Dongle Port set to 0\n" );
				msleep( 1 );

#ifdef CONFIG_ARCH_SDP1406
				if( soc_is_sdp1406uhd( ) && (tztv_hdmi_isOCMConnected == BOSTON_PLUG) )
				{
					do
					{
						ret = Boston_Gpio_Set( BOSTON_USB_ENABLE, BOSTON_USB_SET_LOW );
						msleep( 10 );
					} while( ret && ((++cnt) <= BOSTON_MAX_CTRL_TRY) );
					printk( KERN_ERR "Boston GPIO set to 0 - [%d][%d]\n", ret, cnt );
				}
#endif
			}
			if( soc_is_sdp1404( ) )
			{
				usben_reset(SET_TV_B_MOIP_LOW);
//				usben_reset(SET_INTERNAL_MOIP_LOW);		/* Camera app. set to low for internal MoIP */
				printk( KERN_ERR "TV board MOIP Port set to 0\n" );
			}

			gpio_set_value( (u32)gpio, 0 );
			printk( KERN_ERR "USB Enabled GPIO set to 0\n" );
			msleep( 1 );
		}
		break;

	case IOCTL_USBEN_GET:
		if( !ioctl_param )
		{
			printk( "Wrong msg to get GPIO\n" );
			return -1;
		}

		read_status = (int*) ioctl_param;
		copy_to_user( read_status, &current_gpio_val, 8 );
		break;
		
	case IOCTL_USBEN_RESET_BT:
		usben_reset(RESET_BT);
		break;
		
	case IOCTL_USBEN_RESET_WIFI:
		usben_reset(RESET_WIFI);
		break;
		
	case IOCTL_USBEN_RESET_MOIP:
		usben_reset(RESET_MOIP);
		break;

	case IOCTL_USBEN_SET_BT_LOW:
		usben_reset(SET_BT_LOW);
		break;

	case IOCTL_USBEN_SET_WIFI_LOW:
		usben_reset(SET_WIFI_LOW);
		break;
	}

	return 0;
}


static const struct file_operations usben_gpio_fileops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = usben_ioctl,
};

static const struct of_device_id usben_match[] = {
	{ .compatible = "samsung,sdp-usben" },
	{},
};
MODULE_DEVICE_TABLE(of, usben_match);


static int usben_probe( struct platform_device *pdev )
{
	int retval;

	gpio = of_get_named_gpio( pdev->dev.of_node, "samsung,usb-enable", 0 );
	if( !gpio_is_valid(gpio) )
	{
		return -10;
	}

	current_gpio_val = 1;

	printk( KERN_ERR "of_node Name = %s\n", (pdev->dev.of_node)->name );
	printk( KERN_ERR "of_node Type = %s\n", (pdev->dev.of_node)->type );

	usben_id = MKDEV(MAJOR_NUM, 0);
	retval = register_chrdev_region( usben_id, 1, "USBEN_gpio" );
	if( retval < 0 )
	{
		printk( KERN_ERR "Problem in getting the Major Number\n" );
		return -11;
	}

	cdev_init( &usben_gpio_cdev, &usben_gpio_fileops );
	cdev_add( &usben_gpio_cdev, usben_id, 1 );

	return 0;
}


static struct platform_driver usben_driver = {
	.probe	= usben_probe,
	.driver	= {
		.name			= "sdp-usben",
		.owner			= THIS_MODULE,
		.bus			= &platform_bus_type,
		.of_match_table	= of_match_ptr(usben_match),
	},
};


static int __init usben_init( void )
{
	int retval = 0;

	retval = platform_driver_register( &usben_driver );

	return retval;
}

static int __exit usben_exit( void )
{
	cdev_del( &usben_gpio_cdev );
	unregister_chrdev_region( usben_id, 1 );
	platform_driver_unregister( &usben_driver );

	return 0;
}

MODULE_ALIAS("platform:sdp-usben");
module_init(usben_init);
module_exit(usben_exit);
MODULE_LICENSE("GPL");

