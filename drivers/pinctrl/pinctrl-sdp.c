/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <mach/soc.h>

//#define EEPROM_WP_DEBUG
static DEFINE_MUTEX(gpio_lock);

/* register offsets */
#define SDP_GPIO_CON		0x0
#define SDP_GPIO_PWDATA		0x4
#define SDP_GPIO_PRDATA		0x8

struct sdp_gpio_bank {
	struct sdp_pinctrl_data *pinctrl;
	struct gpio_chip	chip;
	char			label[8];
	unsigned int		nr_gpios;
	unsigned int		reg_offset;
	spinlock_t 			port_lock;
	unsigned long		output;
};

struct sdp_pinctrl_data {
	void __iomem		*base;
	struct sdp_gpio_bank	*gpio_bank;
	unsigned int		nr_banks;
	unsigned int		start_offset;

	struct pinctrl_desc	*ctrldesc;
	/* TODO */
};

#define SDP_GPIO_BANK(name, nr, offset)		\
	{					\
		.label		= name,		\
		.nr_gpios	= nr,		\
		.reg_offset	= offset,	\
	}

static struct sdp_gpio_bank *sdp_gpio_banks;

#define MAX_CHIP_NUM 10
static int num_chip;
static u32 pad_save[MAX_CHIP_NUM][256];
static u32 chip_sel[MAX_CHIP_NUM]={1,1,1,1,1,1,1,1,1,1};
static struct sdp_pinctrl_data *sdp_pinctrl[MAX_CHIP_NUM];

static struct gpio_save
{
	void __iomem * start;
	u32 size;
}gpio_save_t[MAX_CHIP_NUM];

static inline struct sdp_gpio_bank *chip_to_bank(struct gpio_chip *chip)
{
	return container_of(chip, struct sdp_gpio_bank, chip);
}
#ifdef EEPROM_WP_DEBUG
#ifndef CONFIG_VD_RELEASE 
static int eeprom_wp_set;
#endif
#endif
static void sdp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u8 data;

	spin_lock_irqsave(&gpio_bank->port_lock,flags);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;
#ifdef EEPROM_WP_DEBUG
#ifndef CONFIG_VD_RELEASE 	
	if(soc_is_sdp1304())//check GolfP eeprom WP L->L->H
	{
		if(gpio_bank->reg_offset == 0x98 && offset ==1 && value == 0)		eeprom_wp_set ++; 
		else if (gpio_bank->reg_offset == 0x98 && offset ==1 && value == 1 && eeprom_wp_set == 2)	{	printk("P0.1 set LOW -> LOW -> HIGH .....%d\n",eeprom_wp_set); BUG_ON(1);}
		else if (gpio_bank->reg_offset == 0x98 && offset ==1 && value == 1)	eeprom_wp_set = 0;
	}
#endif
#endif
	data = readb(reg + SDP_GPIO_PWDATA);
	data &= ~(1 << offset);
	if (value)
		data |= 1 << offset;

	writeb(data, reg + SDP_GPIO_PWDATA);
	
	spin_unlock_irqrestore(&gpio_bank->port_lock,flags);
	
}

static int sdp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	void __iomem *reg;
	u8 data;
	
	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	if (test_bit(offset, &gpio_bank->output))
		data = readb(reg + SDP_GPIO_PWDATA);
	else
		data = readb(reg + SDP_GPIO_PRDATA);
	data >>= offset;
	data &= 1;

	return data;
}

static int sdp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u32 data;

	__clear_bit(offset, &gpio_bank->output);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	spin_lock_irqsave(&gpio_bank->port_lock,flags);

	data = readl(reg);
	data &= ~(0x3 << (offset * 4));
	data |= 0x2 << (offset * 4);

	writel(data, reg);
	
	spin_unlock_irqrestore(&gpio_bank->port_lock,flags);

	return 0;

	/*
	 * TODO
	 * return pinctrl_gpio_direction_input(chip->base + offset);
	 */
}

static int sdp_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u32 data;
	
	__set_bit(offset, &gpio_bank->output);
	sdp_gpio_set(chip, offset, value);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	spin_lock_irqsave(&gpio_bank->port_lock,flags);

	data = readl(reg);
	data |= 0x3 << (offset * 4);

	writel(data, reg);

	spin_unlock_irqrestore(&gpio_bank->port_lock,flags);

	return 0;

	/*
	 * TODO
	 * return pinctrl_gpio_direction_output(chip->base + offset);
	 */
}

int suspend_gpio_recovery(unsigned int gpionum,bool level)
{
	int i,cnt;
	int port,pin;
	u32 bank = 0;

	mutex_lock(&gpio_lock);

	for(i=0;i<num_chip;i++)
	{
		bank+=(sdp_pinctrl[i]->nr_banks-1)*8+8;
		if(gpionum < bank)
		{ 			
			if(i != 0)
			{ 	
				gpionum = gpionum-bank;
				break;
			}
			break;	
		}
		
	}
	
	port = gpionum/8;
	pin = gpionum%8;

	cnt = sdp_pinctrl[i]->start_offset/0x4+port*3+1;
	
	if(level)			pad_save[i][cnt]|=(1<<(16+pin));
	else 	pad_save[i][cnt]|=(1<<(8+pin));
	
	mutex_unlock(&gpio_lock);
	
	return 0;

}
EXPORT_SYMBOL(suspend_gpio_recovery);

static const struct gpio_chip sdp_gpio_chip = {
	.set			= sdp_gpio_set,
	.get			= sdp_gpio_get,
	.direction_input	= sdp_gpio_direction_input,
	.direction_output	= sdp_gpio_direction_output,
	.owner			= THIS_MODULE,
};

static int sdp_gpiolib_register(struct platform_device *pdev,
				struct sdp_pinctrl_data *pinctrl)
{
	struct sdp_gpio_bank *gpio_bank;
	struct gpio_chip *chip;
	static int cnt=0;
	static int base = 0;
	int ret;
	int i;
	unsigned int reg_offset = pinctrl->start_offset;

	gpio_bank=pinctrl->gpio_bank+cnt;
	for (i=0; i < (int) pinctrl->nr_banks; i++,gpio_bank++,cnt++) {
		struct device_node *node = pdev->dev.of_node;
		struct device_node *np;

		spin_lock_init(&gpio_bank->port_lock);
		
		gpio_bank->chip = sdp_gpio_chip;
		gpio_bank->pinctrl = pinctrl;
		gpio_bank->reg_offset = reg_offset;
		gpio_bank->nr_gpios = 8;

		chip = &gpio_bank->chip;
		chip->dev = &pdev->dev;
		chip->base = base;
		chip->ngpio = (u16) gpio_bank->nr_gpios;
		snprintf(gpio_bank->label, 8, "gpio%d", cnt);
		
		chip->label = gpio_bank->label;

		for_each_child_of_node(node, np) {
			if (!of_find_property(np, "gpio-controller", NULL))
				continue;
			if (!strcmp(gpio_bank->label, np->name)) {
				chip->of_node = np;
				break;
			}
		}

		ret = gpiochip_add(chip);
		if (ret) {
			dev_err(&pdev->dev, "failed to register gpio_chip\n");
			goto err;
		}

		base += gpio_bank->nr_gpios;
		reg_offset += 0xC;
	}

	return 0;

err:
	for (--i, --gpio_bank; i >= 0; i--, gpio_bank--)
		if (gpiochip_remove(&gpio_bank->chip))
			dev_err(&pdev->dev, "gpio_chip remove failed\n");

	return ret;
}

static void sdp_gpiolib_unregister(struct platform_device *pdev,
				   struct sdp_pinctrl_data *pinctrl)
{
	struct sdp_gpio_bank *gpio_bank = pinctrl->gpio_bank;
	int error;
	u32 i;

	for (i = 0; i < pinctrl->nr_banks; i++, gpio_bank++) {
		error = gpiochip_remove(&gpio_bank->chip);
		if (error)
			dev_warn(&pdev->dev, "failed to register gpio_chip\n");
	}
}

static struct pinctrl_desc sdp_pinctrl_desc = {
	/* TODO */
};

static int sdp_pinctrl_register(struct platform_device *pdev,
				struct sdp_pinctrl_data *pinctrl)
{
	/* TODO */
	printk("[%d] %s\n", __LINE__, __func__);
	return 0;
}

static void sdp_pinctrl_unregister(struct platform_device *pdev,
				   struct sdp_pinctrl_data *pinctrl)
{
	/* TODO */
	printk("[%d] %s\n", __LINE__, __func__);
}

static int sdp_pinctrl_check_platfrom(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if(of_property_read_u32_array(dev->of_node, "model-sel",chip_sel,num_chip)==0)
	{
		printk("gpio platform init\n");
	}

	return 0;
}

static int sdp_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret,i;
	u32 total_bank=0;
	u32 *value;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	for(i=0;i<MAX_CHIP_NUM;i++)
	{
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			if(i==0)dev_err(dev, "cannot find IO resource\n");

			break;
		}

		sdp_pinctrl[i] = devm_kzalloc(dev, sizeof(struct sdp_pinctrl_data ), GFP_KERNEL);
		if (!sdp_pinctrl[i]) {
			dev_err(dev, "failed to allocate memory\n");
			return -ENOMEM;
		}

		sdp_pinctrl[i]->base = devm_request_and_ioremap(&pdev->dev, res);

		if (!sdp_pinctrl[i]->base) {
			dev_err(dev, "ioremap failed\n");
		}

		gpio_save_t[i].start = sdp_pinctrl[i]->base;
		gpio_save_t[i].size = (u32) res->end-res->start;

	}
	
		num_chip=i;

		value = devm_kzalloc(dev, sizeof(u32)*num_chip, GFP_KERNEL);
		if (of_property_read_u32_array(dev->of_node, "nr-banks", value,num_chip))
		{
			
		}
		for(i=0;i<num_chip;i++)
		{
			sdp_pinctrl[i]->nr_banks=value[i];
			total_bank += sdp_pinctrl[i]->nr_banks;	
		}
		if (of_property_read_u32_array(dev->of_node, "start-offset", value,num_chip))
		{
			
		}
		for(i=0;i<num_chip;i++)
		{
			sdp_pinctrl[i]->start_offset=value[i];
		}
			
		sdp_gpio_banks = devm_kzalloc(dev,sizeof(struct sdp_gpio_bank) * (total_bank),GFP_KERNEL);
		if (!sdp_gpio_banks) {
			dev_err(dev, "failed to allocate memory\n");
			return -ENOMEM;
		}
		for(i=0;i<num_chip;i++)
		{
			sdp_pinctrl[i]->gpio_bank = sdp_gpio_banks;
			sdp_pinctrl[i]->ctrldesc = &sdp_pinctrl_desc;
				

			ret = sdp_gpiolib_register(pdev, sdp_pinctrl[i]);
			if (ret)
				return ret;
		}

	ret = sdp_pinctrl_register(pdev, sdp_pinctrl[i]);
	if (ret) {
		sdp_gpiolib_unregister(pdev, sdp_pinctrl[i]);
		return ret;
	}
	platform_set_drvdata(pdev, sdp_pinctrl);
	sdp_pinctrl_check_platfrom(pdev);

	return 0;
}

static int sdp_pinctrl_remove(struct platform_device *pdev)
{
	struct sdp_pinctrl_data *pinctrl = platform_get_drvdata(pdev);

	sdp_gpiolib_unregister(pdev, pinctrl);
	sdp_pinctrl_unregister(pdev, pinctrl);

	return 0;
}

#ifdef CONFIG_PM
static int sdp_pinctrl_suspend(struct device *dev)
{
	void __iomem *reg;
	int i,j,k;

	for(i=0;i<num_chip;i++)
		{	
			if(((chip_sel[i])&1))
			{	
				for(j=0;j<sdp_pinctrl[i]->start_offset/4;j++) //pad 
				{	
					reg=(void __iomem *)(sdp_pinctrl[i]->base+j*4);
					pad_save[i][j]=readl(reg);
				}
				for(k=0;k<sdp_pinctrl[i]->nr_banks;k++) //gpio
				{	
					reg = (void __iomem *)(sdp_pinctrl[i]->base + sdp_pinctrl[i]->start_offset+k*0xC);
					pad_save[i][j+k*3]=readl(reg);
					pad_save[i][j+k*3+1]&=~0xFF;	
					pad_save[i][j+k*3+1]|=(readl(reg+0x4)&0xff);	
				}
			}
		}


	return 0;
}

static int sdp_pinctrl_resume(struct device *dev)
{
	void __iomem *reg;
	int i,j,k,l;
	u32 val;

	for(i=0;i<num_chip;i++)
	{
		if(((chip_sel[i])&1))
		{	
			for(j=0;j<sdp_pinctrl[i]->start_offset/4;j++) //pad 
			{	
				reg=(void __iomem *)(sdp_pinctrl[i]->base+j*4);
				writel(pad_save[i][j],reg);
			}

			for(k=0;k<sdp_pinctrl[i]->nr_banks;k++) //gpio
			{	
				reg=(void __iomem *)(sdp_pinctrl[i]->base+sdp_pinctrl[i]->start_offset+k*0xC);
				
				if(pad_save[i][j+k*3+1]>>8)
				{
					val = ((pad_save[i][j+k*3+1]>>8) & 0xff) |
						((pad_save[i][j+k*3+1]>>16) & 0xff);
					for(l=0;l<8;l++)
					{
						if((val>>l)&0x1)
						{	
							pad_save[i][j+k*3]|=(0x3<<l*4);
						}
					}	
					writel(pad_save[i][j+k*3],reg);
				}else		writel(pad_save[i][j+k*3],reg);

				pad_save[i][j+k*3+1]&=(~(pad_save[i][j+k*3+1]&0xff00)>>8);
				pad_save[i][j+k*3+1]|=(pad_save[i][j+k*3+1]>>16)&0xff;
				writel(pad_save[i][j+k*3+1]&0xff,reg+0x4);
			}	
		}
	}
	
	return 0;

}
#endif
static const struct of_device_id sdp_pinctrl_dt_match[] = {
	{ .compatible = "samsung,sdp-pinctrl", },
	{},
};
#ifdef CONFIG_PM
static const struct dev_pm_ops sdp_pinctrl_pm_ops = {
	.suspend_late = sdp_pinctrl_suspend,
	.resume_early = sdp_pinctrl_resume,
	//.suspend = sdp_pinctrl_suspend,
	//.resume = sdp_pinctrl_resume,
};
#define SDP_PINCTRL_PM_OPS (&sdp_pinctrl_pm_ops)
#else /* !CONFIG_PM */
#define SDP_PINCTRL_PM_OPS NULL
#endif /* !CONFIG_PM */

MODULE_DEVICE_TABLE(of, sdp_pinctrl_dt_match);

static struct platform_driver sdp_pinctrl_driver = {
	.probe		= sdp_pinctrl_probe,
	.remove		= sdp_pinctrl_remove,
	.driver = {
		.name	= "sdp-pinctrl",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_pinctrl_dt_match),
		#ifdef CONFIG_PM
		.pm = SDP_PINCTRL_PM_OPS,
		#endif 
	},
};

static int __init sdp_pinctrl_init(void)
{
	return platform_driver_register(&sdp_pinctrl_driver);
}
postcore_initcall(sdp_pinctrl_init);

static void __exit sdp_pinctrl_exit(void)
{
	platform_driver_unregister(&sdp_pinctrl_driver);
}
module_exit(sdp_pinctrl_exit);

MODULE_DESCRIPTION("Samsung SDP SoCs pinctrl driver");
MODULE_LICENSE("GPL v2");

