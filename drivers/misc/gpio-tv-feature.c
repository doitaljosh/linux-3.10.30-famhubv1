#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/err.h>

static struct class *gpio_tv_class;
static struct device *gpio_tv_dev, *gpio_dts_dev;

struct gpio_data_map {
	struct device_attribute *dev_attr;
	char name[30];
};

static DEFINE_MUTEX(gpio_mutex);

static struct gpio_data_map * find_gpio_map(struct device_attribute *attr);

static ssize_t gpio_tv_feature_show(struct device *dev,
                      struct device_attribute *attr, char *buf)
{
	int gpio = 0, ret = 0;
	int val;
	struct gpio_data_map *map = find_gpio_map(attr);
	struct device *dts_dev = gpio_dts_dev;

	dev_dbg(dts_dev, "[%s] called.\n", __func__);

	if (map == NULL) {
		dev_err(dts_dev, "cannot find gpio attribute!!\n");
		return -ENODEV;
	}

	gpio = of_get_named_gpio(dts_dev->of_node, map->name, 0);
	dev_dbg(dts_dev, "gpio[%d]\n", gpio);
	if (!gpio_is_valid(gpio)) {
		dev_err(dts_dev, "fail to get gpio.\n");
		return -EPERM;
	}

	mutex_lock(&gpio_mutex);
	ret = gpio_request(gpio, map->name);
	if (ret) {
		if (ret == -EBUSY)
			dev_err(dts_dev, "gpio busy: %d\n", ret);
		else
			dev_err(dts_dev,
			"Can't request gpio: %d\n", ret);
		return ret;
	}
	gpio_direction_input(gpio);
	val = gpio_get_value_cansleep(gpio);
	gpio_free(gpio);
	mutex_unlock(&gpio_mutex);

	dev_dbg(dts_dev, "val = %d\n", val);

    	sprintf(buf, "%d", val);
	return 1;
}

static ssize_t gpio_tv_feature_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int gpio = 0, ret = 0;
	int val;
	struct gpio_data_map *map = find_gpio_map(attr);
	struct device *dts_dev = gpio_dts_dev;

	dev_dbg(dts_dev, "[%s] called.\n", __func__);

	if (map == NULL) {
		dev_err(dts_dev, "cannot find gpio attribute!!\n");
		return -ENODEV;
	}

	sscanf(buf, "%d", &val);
	if ((val != 0) && (val != 1)) {
		dev_err(dts_dev, "gpio value should be 0 or 1!!\n");
		return -EINVAL;
	}

	gpio = of_get_named_gpio(dts_dev->of_node, map->name, 0);
	dev_dbg(dts_dev, "gpio[%d]\n", gpio);
	if (!gpio_is_valid(gpio)) {
		dev_err(dts_dev, "fail to get gpio.\n");
		return -EPERM;
	}

	mutex_lock(&gpio_mutex);
	ret = gpio_request(gpio, map->name);
	if (ret) {
		if (ret == -EBUSY)
			dev_err(dts_dev, "gpio busy: %d\n", ret);
		else
			dev_dbg(dts_dev, "Can't request gpio: %d\n", ret);
		return ret;
	}
	gpio_direction_output(gpio, val);
	gpio_free(gpio);
	mutex_unlock(&gpio_mutex);

	return 1;
}

DEVICE_ATTR(ident_av1, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ident_comp, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(usb1_nreset, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(usb2_nreset, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ovd_on_off, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ovd_level, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(nreset_out, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ident_hp, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(led_pdp_sel, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ident_exe_module, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(sub_re_upgrade, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(loopback_hdmi_data, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(loopback_hdmi_en, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(loopback_en, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(loop_usb, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ereq_int, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ident_irb, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(irb_uart_rdy, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(irb_int, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(irb_rst_mcu, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(up_module_usb_sw, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(enable_3d, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(lvds_switch, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(ld_sel, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(hub_select, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(jack_select, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(hv_flip, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);
DEVICE_ATTR(echofs_nreset, S_IWUSR | S_IRUGO, gpio_tv_feature_show, gpio_tv_feature_store);

static struct gpio_data_map gpio_table[] = {
	{ &dev_attr_ident_av1,		"samsung,ident_av1" },
	{ &dev_attr_ident_comp,		"samsung,ident_comp" },
	{ &dev_attr_usb1_nreset,	"samsung,usb1_nreset" },
	{ &dev_attr_usb2_nreset,	"samsung,usb2_nreset" },
	{ &dev_attr_ovd_on_off,		"samsung,ovd_on_off" },
	{ &dev_attr_ovd_level,		"samsung,ovd_level" },
	{ &dev_attr_nreset_out,		"samsung,nreset_out" },
	{ &dev_attr_ident_hp,		"samsung,ident_hp" },
	{ &dev_attr_led_pdp_sel,	"samsung,led_pdp_sel" },
	{ &dev_attr_ident_exe_module,	"samsung,ident_exe_module" },
	{ &dev_attr_sub_re_upgrade,	"samsung,sub_re_upgrade" },
	{ &dev_attr_loopback_hdmi_data,	"samsung,loopback_hdmi_data" },
	{ &dev_attr_loopback_hdmi_en,	"samsung,loopback_hdmi_en" },
	{ &dev_attr_loop_usb,		"samsung,loop_usb" },
	{ &dev_attr_ereq_int,		"samsung,ereq_int" },
	{ &dev_attr_ident_irb,		"samsung,ident_irb" },
	{ &dev_attr_irb_uart_rdy,	"samsung,irb_uart_rdy" },
	{ &dev_attr_irb_int,		"samsung,irb_int" },
	{ &dev_attr_irb_rst_mcu,	"samsung,irb_rst_mcu" },
	{ &dev_attr_up_module_usb_sw,	"samsung,up_module_usb_sw" },
	{ &dev_attr_enable_3d,		"samsung,enable_3d" },
	{ &dev_attr_lvds_switch,	"samsung,lvds_switch" },
	{ &dev_attr_ld_sel,		"samsung,ld_sel" },
	{ &dev_attr_hub_select,		"samsung,hub_select" },
	{ &dev_attr_jack_select,	"samsung,jack_select" },
	{ &dev_attr_hv_flip,		"samsung,hv_flip" },
	{ &dev_attr_echofs_nreset,	"samsung,echofs_nreset" },
};

static struct gpio_data_map * find_gpio_map(struct device_attribute *attr)
{
	int i;
	struct gpio_data_map *map = NULL;
	for (i = 0; i < sizeof(gpio_table)/sizeof(struct gpio_data_map); i++) {
		if (gpio_table[i].dev_attr == attr) {
			map = &gpio_table[i];
			break;
		}
	}
	return map;
}

static int gpio_tv_feature_register_sysfs(struct platform_device *pdev)
{
	int i, ret = 0, gpio;

	gpio_dts_dev = &pdev->dev;

	/* create class. (/sys/class/gpio-tv-feature) ***/
	gpio_tv_class = class_create(THIS_MODULE, "gpio-tv-feature");
	if (IS_ERR(gpio_tv_class)) {
		ret = PTR_ERR(gpio_tv_class);
		goto out;
	}
	/* create class device. (/sys/class/gpio-tv-feature/gpio-tv-feature) */
	gpio_tv_dev = device_create(gpio_tv_class, NULL, pdev->dev.devt, NULL, "gpio-tv-feature");
	if (IS_ERR(gpio_tv_dev)) {
		ret = PTR_ERR(gpio_tv_dev);
		goto out;
	}

	/* create sysfs file. (/sys/class/gpio-tv-feature/gpio-tv-feature/xxxx) */
	for (i = 0; i < sizeof(gpio_table)/sizeof(struct gpio_data_map); i++) {
		gpio = of_get_named_gpio(pdev->dev.of_node, gpio_table[i].name, 0);
		if (gpio >= 0) {
			ret = device_create_file(gpio_tv_dev, gpio_table[i].dev_attr);
			if (ret) {
				dev_err(gpio_dts_dev, "failed to create sysfs.\n");
				goto out;
			}
		}
	}

out:
	return ret;
}

static int gpio_tv_feature_unregister_sysfs(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < sizeof(gpio_table)/sizeof(struct gpio_data_map); i++)
		device_remove_file(gpio_tv_dev, gpio_table[i].dev_attr);
	device_destroy(gpio_tv_class, pdev->dev.devt);
	class_destroy(gpio_tv_class);

	return 0;
}

static int gpio_tv_feature_probe(struct platform_device *pdev)
{
	return gpio_tv_feature_register_sysfs(pdev);
}

static int gpio_tv_feature_remove(struct platform_device *pdev)
{
	return gpio_tv_feature_unregister_sysfs(pdev);
}

static const struct of_device_id gpio_tv_feature_dt_match[] = {
	{ .compatible = "samsung,gpio-tv-feature" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_micom_dt_match);

static struct platform_driver gpio_tv_feature_driver = {
	.probe		= gpio_tv_feature_probe,
	.remove		= gpio_tv_feature_remove,
	.driver		= {
		.name	= "gpio-tv-feautre",
		.owner	= THIS_MODULE,
		.of_match_table	= gpio_tv_feature_dt_match,
	},
};

static int __init gpio_tv_feature_init(void)
{
	return platform_driver_register(&gpio_tv_feature_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(gpio_tv_feature_init);

static void __exit gpio_tv_feature_exit(void)
{
	platform_driver_unregister(&gpio_tv_feature_driver);
}
module_exit(gpio_tv_feature_exit);

MODULE_DESCRIPTION("Samsung GPIO TV feature driver");
MODULE_LICENSE("GPL");

