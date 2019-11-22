#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of_device.h>
#include <linux/gpio.h>

#include <linux/stat.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mutex.h>

#include <linux/workqueue.h>

#define GPIO_ST_EN			0x0004
#define GPIO_BT_EN			0x0102
#define GPIO_USB_DEBUG_EN	0x0200
#define GPIO_CAMERA_EN		0x0302

extern int set_gpio_level(int gpio, int level);

static int usb_debug_irq, camera_irq, bt_irq, st_irq; 

extern int pca953x_port_get_value(unsigned off);
extern int pca953x_port_set_value(unsigned off, int val);

struct work_struct camera_work;    // camera
struct work_struct usb_debug_work; // usb_debug
struct work_struct bt_work;        // blue_tooth
struct work_struct st_work;        // smart_things

static void camera_ocp_bh(struct work_struct *w){
	printk("\n[%s][%d] camera_ocp_bh!\n", __func__, __LINE__);
	set_gpio_level(GPIO_CAMERA_EN, 0);
}

static void usb_debug_ocp_bh(struct work_struct *w){
	printk("\n[%s][%d] usb_debug_ocp_bh!\n", __func__, __LINE__);
	set_gpio_level(GPIO_USB_DEBUG_EN, 0);
}

static void bt_ocp_bh(struct work_struct *w){
	printk("\n[%s][%d] bt_ocp_bh!\n", __func__, __LINE__);
	set_gpio_level(GPIO_BT_EN, 0);
}

static void st_ocp_bh(struct work_struct *w){
	printk("\n[%s][%d] st_ocp_bh!\n", __func__, __LINE__);
	set_gpio_level(GPIO_ST_EN, 0);
}

static irqreturn_t camera_ocp_th(int irq, void *dev_id)
{
	//printk("[%s][%d] camera_ocp_th!\n", __func__, __LINE__);
	schedule_work(&camera_work);
	return IRQ_HANDLED;
}

static irqreturn_t usb_debug_ocp_th(int irq, void *dev_id)
{
	//printk("[%s][%d] usb_debug_ocp_th!\n", __func__, __LINE__);
	schedule_work(&usb_debug_work);
	return IRQ_HANDLED;
}

static irqreturn_t bt_ocp_th(int irq, void *dev_id)
{
	//printk("[%s][%d] bt_ocp_th!\n", __func__, __LINE__);
	schedule_work(&bt_work);
	return IRQ_HANDLED;
}

static irqreturn_t st_ocp_th(int irq, void *dev_id)
{
	//printk("[%s][%d] st_ocp_th! \n", __func__, __LINE__);
	schedule_work(&st_work);
	return IRQ_HANDLED;
}

static int usb_ocp_probe(struct platform_device *pdev)
{
	u32 interrupt_num = 0;
	u32 isel_reg = 0, tmp;
	void __iomem * vbase;
	int ret;	

	/* Camera */
	if (of_property_read_u32(pdev->dev.of_node, "camera_ocp_int", &interrupt_num)){
		printk("[%s][%d] could not get 'camera_ocp_int' node!\n",__func__,__LINE__);
	}
	else {
		camera_irq = 160 + interrupt_num + 32;
		if(of_property_read_u32(pdev->dev.of_node, "extint-reg", &isel_reg)){
			printk("[%s][%d] could not get 'extint-reg' node!\n",__func__,__LINE__);
		}
		else{
#if 0
			vbase = ioremap(isel_reg,0x04);
			tmp = readl((void*)vbase);
			tmp = tmp | (1<< interrupt_num);
			writel(tmp,(void *)vbase);
			iounmap(vbase);
#endif
			printk("[%s][%d] Set EXTINT%d (irq=%d, isel_reg=0x%08x, value=0x%08x)\n",__func__,__LINE__, interrupt_num, camera_irq, isel_reg,tmp);
		}
	}

	/* USB_DEBUG */
	if (of_property_read_u32(pdev->dev.of_node, "usb_debug_ocp_int", &interrupt_num)){
		printk("[%s][%d] could not get 'usb_debug_ocp_int' node!\n",__func__,__LINE__);
	}
	else {
		usb_debug_irq = 160 + interrupt_num + 32;
		if(of_property_read_u32(pdev->dev.of_node, "extint-reg", &isel_reg)){
			printk("[%s][%d] could not get 'extint-reg' node!\n",__func__,__LINE__);
		}
		else{
			vbase = ioremap(isel_reg,0x04);
			tmp = readl((void*)vbase);
			tmp = tmp | (1<< interrupt_num);
			writel(tmp,(void *)vbase);
			iounmap(vbase);

			printk("[%s][%d] Set EXTINT%d (irq=%d, isel_reg=0x%08x, value=0x%08x)\n",__func__,__LINE__, interrupt_num, usb_debug_irq, isel_reg,tmp);
		}
	}

	/* Blue Tooth */
	if (of_property_read_u32(pdev->dev.of_node, "bt_ocp_int", &interrupt_num)){
		printk("[%s][%d] could not get 'bt_ocp_int' node!\n",__func__,__LINE__);
	}
	else {
		bt_irq = 160 + interrupt_num + 32;
		if(of_property_read_u32(pdev->dev.of_node, "extint-reg", &isel_reg)){
			printk("[%s][%d] could not get 'extint-reg' node!\n",__func__,__LINE__);
		}
		else{
			vbase = ioremap(isel_reg,0x04);
			tmp = readl((void*)vbase);
			tmp = tmp | (1<< interrupt_num);
			writel(tmp,(void *)vbase);
			iounmap(vbase);

			printk("[%s][%d] Set EXTINT%d (irq=%d, isel_reg=0x%08x, value=0x%08x)\n",__func__,__LINE__, interrupt_num, bt_irq, isel_reg,tmp);
		}
	}

	/* Smart Things */
	if (of_property_read_u32(pdev->dev.of_node, "st_ocp_int", &interrupt_num)){
		printk("[%s][%d] could not get 'st_ocp_int' node!\n",__func__,__LINE__);
	}
	else {
		st_irq = 160 + interrupt_num + 32;
		if(of_property_read_u32(pdev->dev.of_node, "extint-reg", &isel_reg)){
			printk("[%s][%d] could not get 'extint-reg' node!\n",__func__,__LINE__);
		}
		else{
			vbase = ioremap(isel_reg,0x04);
			tmp = readl((void*)vbase);
			tmp = tmp | (1<< interrupt_num);
			writel(tmp,(void *)vbase);
			iounmap(vbase);

			printk("[%s][%d] Set EXTINT%d (irq=%d, isel_reg=0x%08x, value=0x%08x)\n",__func__,__LINE__, interrupt_num, st_irq, isel_reg,tmp);
		}
	}

	/* Request irq */
	ret = request_irq(camera_irq, camera_ocp_th, IRQF_DISABLED | IRQF_TRIGGER_RISING, dev_name(&pdev->dev),NULL);
	if(!ret)
		printk("[%s][%d] request_irq (Camera) Success\n", __func__, __LINE__);

	ret = request_irq(usb_debug_irq, usb_debug_ocp_th, IRQF_DISABLED | IRQF_TRIGGER_RISING, dev_name(&pdev->dev),NULL);
	if(!ret)
		printk("[%s][%d] request_irq (USB debug) Success\n", __func__, __LINE__);

	ret = request_irq(bt_irq, bt_ocp_th, IRQF_DISABLED | IRQF_TRIGGER_RISING, dev_name(&pdev->dev),NULL);
	if(!ret)
		printk("[%s][%d] request_irq (BT) Success\n", __func__, __LINE__);

	ret = request_irq(st_irq, st_ocp_th, IRQF_DISABLED | IRQF_TRIGGER_RISING, dev_name(&pdev->dev),NULL);
	if(!ret)
		printk("[%s][%d] request_irq (ST) Success\n", __func__, __LINE__);

	INIT_WORK(&camera_work, camera_ocp_bh);
	INIT_WORK(&usb_debug_work, usb_debug_ocp_bh);
	INIT_WORK(&bt_work, bt_ocp_bh);
	INIT_WORK(&st_work, st_ocp_bh);

	return 0;
}

static int usb_ocp_remove(struct platform_device *pdev)
{
	free_irq(usb_debug_irq,NULL);
	free_irq(camera_irq,NULL);
	return 0;
}

static const struct of_device_id usb_ocp_dt_ids[] = {

	{.compatible = "samsung,usb_ocp", },
	{}
};

MODULE_DEVICE_TABLE(of, usb_ocp_dt_ids);


static struct platform_driver usb_ocp_driver = {
	.driver = {
		.name = "usb_ocp",
		.of_match_table = usb_ocp_dt_ids,
	},
	.driver.owner   = THIS_MODULE,
	.probe          = usb_ocp_probe,
	.remove         = usb_ocp_remove,
};

static int usb_ocp_init(void)
{
	return platform_driver_register(&usb_ocp_driver);
}

static void usb_ocp_exit(void)
{

	platform_driver_unregister(&usb_ocp_driver);

}

module_init(usb_ocp_init);
module_exit(usb_ocp_exit);

MODULE_LICENSE("GPL");
