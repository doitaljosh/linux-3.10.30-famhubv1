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

int is_serdes_locked(void);

static int gpio;

int is_serdes_locked(void)
{
	return (!gpio_get_value((u32)gpio));
}
EXPORT_SYMBOL_GPL(is_serdes_locked);
	
static int usbserdes_probe( struct platform_device *pdev )
{
	int retval;

	gpio = of_get_named_gpio( pdev->dev.of_node, "samsung,serdes-lock", 0 );
	if( !gpio_is_valid(gpio) )
	{
		return -10;
	}
	return 0;
}

static const struct of_device_id usbserdes_match[] = {
        { .compatible = "samsung,sdp-usbserdes" },
        {},
};
MODULE_DEVICE_TABLE(of, usbserdes_match);

static struct platform_driver usbserdes_driver = {
	.probe	= usbserdes_probe,
	.driver	= {
		.name			= "sdp-usbserdes",
		.owner			= THIS_MODULE,
		.bus			= &platform_bus_type,
		.of_match_table	= of_match_ptr(usbserdes_match),
	},
};

static int __init usbserdes_init( void )
{
	int retval = 0;
	retval = platform_driver_register( &usbserdes_driver );
	return retval;
}

static int __exit usbserdes_exit( void )
{
	platform_driver_unregister( &usbserdes_driver );
	return 0;
}

MODULE_ALIAS("platform:sdp-usbserdes");
module_init(usbserdes_init);
module_exit(usbserdes_exit);
MODULE_LICENSE("GPL");

