#ifndef _LINUX_PCA953X_H
#define _LINUX_PCA953X_H

#include <linux/types.h>
#include <linux/i2c.h>

/* platform data for the PCA9535 16-bit I/O expander driver */

struct pca953x_platform_data {
	/* number of the first GPIO */
	unsigned	gpio_base;

	/* initial polarity inversion setting */
	u32		invert;

	/* interrupt base */
	int		irq_base;

	void		*context;	/* param to setup/teardown */

	int		(*setup)(struct i2c_client *client,
				unsigned gpio, unsigned ngpio,
				void *context);
	int		(*teardown)(struct i2c_client *client,
				unsigned gpio, unsigned ngpio,
				void *context);
	const char	*const *names;
};


/* configration output value */

#define HIGH (1)
#define LOW (0)

#define P0_0_OUT HIGH // LCD_EN
#define P0_1_OUT HIGH // USB_DEBUG_EN
#define P0_2_OUT LOW // WIFI_EN
#define P0_3_OUT HIGH // CAM1_EN
#define P0_4_OUT LOW  // Not Used 
#define P0_5_OUT HIGH // TOUCHKEY_BL1
#define P0_6_OUT HIGH // TOUCHKEY_BL2
#define P0_7_OUT LOW  // Not Used
#define P1_0_OUT HIGH // SENSOR_EN
#define P1_1_OUT HIGH // TOUCHKEY_BL0
#define P1_2_OUT LOW // YMU_SPIMODE
#define P1_3_OUT HIGH // CODEC_EN
#define P1_4_OUT LOW // Not Used 
#define P1_5_OUT LOW // Not Used 
#define P1_6_OUT LOW // Not Used 
#define P1_7_OUT LOW // Not Used 

#endif /* _LINUX_PCA953X_H */
