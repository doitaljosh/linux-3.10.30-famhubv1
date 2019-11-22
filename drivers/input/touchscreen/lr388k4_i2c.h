#ifndef _LR388K4_I2C_H
#define _LR388K4_I2C_H

#define LR388K4_I2C_DRIVER_NAME      "lr388k4_i2c"

// hiroshi@sharp
#define OMAP4PANDA_GPIO_LR388K4_IRQ0 59 // pin#28
#define OMAP4PANDA_GPIO_LR388K4_RESET 37 // pin#10 // not used for K4

#define LR388K4_I2C_SLAVE_ADDRESS 0x60 // 7bit value - 0xC0 for write, 0xC1 for read
#define LR388K4_I2C_BUS 8
#define IRQ_MT 177

static int lr388k4_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int lr388k4_i2c_remove(struct i2c_client *client);
static int lr388k4_i2c_init(void);
static void lr388k4_i2c_exit(void);


struct lr388k4_i2c_pdata
{
	int reset_pin;		/* Reset pin is wired to this GPIO (optional) */
	int irq_pin;		/* IRQ pin is wired to this GPIO */
};

#endif /* _LR388K4_I2C_H */
