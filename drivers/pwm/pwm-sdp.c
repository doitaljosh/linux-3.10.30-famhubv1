/*
 * Copyright (C) 2015 Samsung Electronics, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from freescale PWM driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of_device.h>



//custom hw register (tmp) for i2e port open
#define REG_I2E_PORT			(0x005C1040)
#define		PAD_B_GPIO_I2E_PWM	5

#define REG_VSYNC_INITIAL_ON	0x00	// [0] V sync 단위로 I2E 출력 신호를 초기화 - 0x1
#define REG_PWM_BRI_GAIN		0x14	// [10:0] 외부 광량에 따른 PWM gain control(1024:1.0 ~ 0:0.0) 0x400
#define REG_PWM1_MAX 			0x4C	// [10:0] PWM1 dimming max width(except up peak width) - 0x19A
#define REG_PWM_MIN_LIMIT 		0x18 	// [10:0] PWM dimming min limit width(except slow in and out width) - 0x47
#define REG_PWM1_TOTAL 		0x84	// [10:0] PWM1 dimming total width - 0x3E7
#define REG_POFF1_WIDTH 		0x88	// [10:0] I2E off시 PWM1 dimming width - 0x3E7
#define REG_PWM1_CNT			0x8C	// [15:0] PWM1 dimming의 duty를 결정하는 count 값 - 0x4E9


#define MAX_PWM_GAIN			1023

struct sdp_chip {
	void __iomem	*mmio_base;

	struct pwm_chip	chip;

	int (*config)(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns);
	void (*set_enable)(struct pwm_chip *chip, bool enable);	
};

#define to_sdp_chip(chip)	container_of(chip, struct sdp_chip, chip)


static void _sdp_pwm_set_enable(struct pwm_chip *chip, bool enable)
{
/*
	struct sdp_chip *sdp = to_sdp_chip(chip);
	u32 val;


	if (enable)
		val |= MX1_PWMC_EN;
	else
		val &= ~MX1_PWMC_EN;

	writel(val, PAD_CTRL_17);
*/	
}

static int sdp_pwm_config(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct sdp_chip *sdp = to_sdp_chip(chip);
	u32 max = MAX_PWM_GAIN;
	u32 p = max * duty_ns / period_ns;
	
	writel(p, sdp->mmio_base + REG_POFF1_WIDTH);

	return 0;
}

static int sdp_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	_sdp_pwm_set_enable(chip, true);

	return 0;
}

static void sdp_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	_sdp_pwm_set_enable(chip, false);
}

static struct pwm_ops sdp_pwm_ops = {
	.enable = sdp_pwm_enable,
	.disable = sdp_pwm_disable,
	.config = sdp_pwm_config,
	.owner = THIS_MODULE,
};

static const struct of_device_id sdp_pwm_dt_ids[] = {
	{ .compatible = "samsung,sdp-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdp_pwm_dt_ids);

static int sdp_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(sdp_pwm_dt_ids, &pdev->dev);
	struct sdp_chip *sdp;
	struct resource *r;
	u32 val;
	int ret = 0;
	void __iomem *reg_i2e_port = ioremap(REG_I2E_PORT,0x4);

	if (!of_id)
		return -ENODEV;

	sdp = devm_kzalloc(&pdev->dev, sizeof(*sdp), GFP_KERNEL);
	if (sdp == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	sdp->chip.ops = &sdp_pwm_ops;
	sdp->chip.dev = &pdev->dev;
	sdp->chip.base = -1;
	sdp->chip.npwm = 1;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r){
		dev_err(&pdev->dev, "cannot find IO resource\n");
		return -ENOENT;
	}
	
	sdp->mmio_base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (IS_ERR(sdp->mmio_base))
		return PTR_ERR(sdp->mmio_base);

	ret = pwmchip_add(&sdp->chip);
	if (ret < 0)
		return ret;

	/* pin mux set */
	val = readl(reg_i2e_port);
	val |= ((u32)0x01<<PAD_B_GPIO_I2E_PWM);
	writel(val, reg_i2e_port);

	writel(0,sdp->mmio_base + REG_PWM_MIN_LIMIT);
	writel(1023,sdp->mmio_base + REG_PWM1_TOTAL);
	writel(1023,sdp->mmio_base + REG_PWM1_MAX);
	writel(1,sdp->mmio_base + REG_VSYNC_INITIAL_ON);
	writel(1023,sdp->mmio_base + REG_PWM_BRI_GAIN);
	// 1208 = 120Hz
	// 2416 = 60Hz
	writel(1208, sdp->mmio_base + REG_PWM1_CNT);

	platform_set_drvdata(pdev, sdp);
	
	return 0;
}

static int sdp_pwm_remove(struct platform_device *pdev)
{
	struct sdp_chip *sdp;

	sdp = platform_get_drvdata(pdev);
	if (sdp == NULL)
		return -ENODEV;

	return pwmchip_remove(&sdp->chip);
}

static struct platform_driver sdp_pwm_driver = {
	.driver		= {
		.name	= "sdp-pwm",
		.of_match_table = of_match_ptr(sdp_pwm_dt_ids),
	},
	.probe		= sdp_pwm_probe,
	.remove		= sdp_pwm_remove,
};

module_platform_driver(sdp_pwm_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("jhee.jeong <jhee.jeong@samsung.com>");
