/*
 * Driver for sharp touch screen controller
 *
 * Copyright (c) 2013 Sharp Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/types.h>
#include <linux/delay.h>
#include "lr388k4_i2c.h"
#include <linux/platform_device.h>

/* Touch coordinates */
#define LR388K4_I2C_X_MIN		0
#define LR388K4_I2C_Y_MIN		0
#define LR388K4_I2C_X_MAX		15360
#define LR388K4_I2C_Y_MAX		8640
#define LR388K4_I2C_P_MIN		0
#define LR388K4_I2C_P_MAX		(0xff)


/* DEBUG */
#if 0 
#ifndef DEBUG_LR388K4
#define DEBUG_LR388K4
#endif//DEBUG_LR388K4
#endif /* 0 */

/* Touch Parmeters */
#define LR388K4_REPORT_CONTACT_COUNT 35
#define LR388K4_REPORT_LENGTH (64+2)
#define LR388K4_LENGTH_OF_TOUCH 6
#define LR388K4_REPORT_HEADER_SIZE 3
#define LR388K4_REPORT_TIP 0x01
#define LR388K4_REPORT_CONTACTID_OFFSET 1
#define LR388K4_REPORT_X_OFFSET 2
#define LR388K4_REPORT_Y_OFFSET 4
#define LR388K4_REPORT_REPORT_ID 2
#define LR388K4_REPORT_REPORT_ID_VAL 0x81

#define LR388K4_MAX_TOUCH_1PAGE 5

/* Touch Status */
#define LR388K4_F_TOUCH ((u8)0x01)
#define LR388K4_F_TOUCH_OUT ((u8)0x03)

static u8 TotalTouch;
static u8 PartialTouch;

/*  The touch driver structure */
struct lr388k4_touch {
  u8 status;
  u8 id;
  u8 size;
  u16 x;
  u16 y;
  u16 z;
};

struct lr388k4_i2c {
	struct input_dev *input;
	char phys[32];
	struct i2c_client *client;
	struct i2c_client *client_sub;
	int reset_pin;
	int irq0_pin;
	int irq1_pin;
        struct lr388k4_touch touch[20];

        unsigned int            max_num_touch;
        int                     min_x;
        int                     min_y;
        int                     max_x;
        int                     max_y;
        int                     pressure_max;
        int                     touch_num_max;
        bool                    flip_x;
        bool                    flip_y;
        bool                    swap_xy;
};


static const struct of_device_id lr388k4_of_match[] = {
	{ .compatible = "sharp,lr388k4_i2c" },
	{}
};
MODULE_DEVICE_TABLE(of, lr388k4_of_match);


static struct i2c_device_id lr388k4_i2c_idtable[] = {
	{ LR388K4_I2C_DRIVER_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lr388k4_i2c_idtable);

static struct i2c_driver lr388k4_i2c_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= LR388K4_I2C_DRIVER_NAME,
		.of_match_table = of_match_ptr(lr388k4_of_match),
	},
	.id_table	= lr388k4_i2c_idtable,
	.probe		= lr388k4_i2c_probe,
	.remove		= lr388k4_i2c_remove,
};



static int lr388k4_i2c_read_regs(struct lr388k4_i2c *tsc,
				 unsigned char *data, unsigned char len)
{
	struct i2c_client *client = tsc->client;
	i2c_master_recv(client, data, len);

	return 0;
}

static void ReadMultiBytes(struct lr388k4_i2c *ts, u8 u8Len, u8 *u8Buf)
{
	lr388k4_i2c_read_regs(ts, u8Buf, u8Len);
}

static u8 GetNumOfTouch(char *buf)
{
  return buf[LR388K4_REPORT_CONTACT_COUNT];
}


static void GetTouchReport(struct lr388k4_i2c *ts)
{
  struct lr388k4_touch *touch = ts->touch;
  struct input_dev *input_dev = ts->input;
  u8 reportBuffer[128];
  int i;
  u8 ID,Status;

  ReadMultiBytes(ts, LR388K4_REPORT_LENGTH, reportBuffer);

  if (reportBuffer[LR388K4_REPORT_REPORT_ID] != LR388K4_REPORT_REPORT_ID_VAL) {
    return;
  }

  if ((1 <= GetNumOfTouch(reportBuffer))  &&
      (GetNumOfTouch(reportBuffer) <= LR388K4_MAX_TOUCH_1PAGE)) {
    /* 1 page (<=5) or  1st page of two pages */
    TotalTouch = PartialTouch = GetNumOfTouch(reportBuffer);
  } else if (GetNumOfTouch(reportBuffer) == 0) {
    /* 2nd page of two pages */
    /* keep TotalTouch */
    PartialTouch = TotalTouch - LR388K4_MAX_TOUCH_1PAGE;
  } else {
    /* 1st page of two pages */
    TotalTouch = GetNumOfTouch(reportBuffer);
    PartialTouch = LR388K4_MAX_TOUCH_1PAGE;
  }

#if defined(DEBUG_LR388K4)
    printk(KERN_ALERT "[SHARP_TOUCH]TotalTouch: %d, PartialTouch: %d\n", TotalTouch, PartialTouch);
#endif  

  for(i = 0; i < PartialTouch; i++){
    Status = (reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH]
	      & LR388K4_REPORT_TIP);
    touch[i].id = ID = reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH
				    + LR388K4_REPORT_CONTACTID_OFFSET];
    touch[i].x =
      (reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH
		    + LR388K4_REPORT_X_OFFSET] << 0) |
      (reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH
		    + LR388K4_REPORT_X_OFFSET + 1] << 8);
    touch[i].y =
      (reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH
		    + LR388K4_REPORT_Y_OFFSET] << 0) |
      (reportBuffer[LR388K4_REPORT_HEADER_SIZE + i * LR388K4_LENGTH_OF_TOUCH
		    + LR388K4_REPORT_Y_OFFSET + 1] << 8);
    
#if defined(DEBUG_LR388K4)
    printk(KERN_ALERT "[SHARP_TOUCH]shtp_lr388k4 ID=%2d, Status=%02x, X=%5d, Y=%5d, total=%2d, partial=%2d\n"
	   ,ID
	   ,Status
	   ,touch[i].x
	   ,touch[i].y
	   ,TotalTouch
	   ,PartialTouch);
#endif

    input_mt_slot(input_dev, ID);
    if(! Status) { // not touched
      touch[i].status = LR388K4_F_TOUCH_OUT;
      input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false); // the 2nd parameter is DON'T CARE
      continue;
    }

    if(ts->swap_xy){
      int tmp;
      tmp = touch[i].x;
      touch[i].x = touch[i].y;
      touch[i].y = tmp;
    }
    if(ts->flip_x){
      touch[i].x = (ts->max_x - touch[i].x) < 0 ? 0 : ts->max_x - touch[i].x;
    }
    if(ts->flip_y){
      touch[i].y = (ts->max_y - touch[i].y) < 0 ? 0 : ts->max_y - touch[i].y;
    }

    touch[i].status = LR388K4_F_TOUCH;
    
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);

    input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 0xff);
    input_report_abs(input_dev, ABS_MT_POSITION_X, touch[i].x);
    input_report_abs(input_dev, ABS_MT_POSITION_Y, touch[i].y);
    input_report_abs(input_dev, ABS_MT_PRESSURE  , LR388K4_I2C_P_MAX);
  }

  if (PartialTouch <= LR388K4_MAX_TOUCH_1PAGE) {
    /* one page only(<=5) or 2nd page(0) */
#if defined(DEBUG_LR388K4)
    printk(KERN_ALERT "[SHARP_TOUCH]shtp_lr388k4 flush touch input(%d, %d)\n", TotalTouch, PartialTouch);
#endif
    input_sync(ts->input);
  } else {
    /* 1st page of two pages(6-10) */
    // do nothing
#if defined(DEBUG_LR388K4)
    printk(KERN_ALERT "[SHARP_TOUCH]shtp_lr388k4 NOT flush touch input(%d)\n", TotalTouch);
#endif
  }
}


static irqreturn_t lr388k4_i2c_irq_thread(int irq, void *_ts)
{
	struct lr388k4_i2c *ts = _ts;
	static bool bCalled = false;

	if(!bCalled){
#if defined(DEBUG_LR388K4)
	  printk(KERN_ALERT "[SHARP_TOUCH][Enter]shtp_lr388k4_irq\n");
#endif
	  bCalled = true;
	}

	/* Retrieve touch report and send it to the kernel */
	GetTouchReport(ts);

	return IRQ_HANDLED;
}
static int lr388k4_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	//const struct lr388k4_i2c_pdata *pdata = client->dev.platform_data;
	struct lr388k4_i2c *ts;
	struct input_dev *input_dev;
	int err;

	ts = kzalloc(sizeof(struct lr388k4_i2c), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}
	
	struct i2c_adapter *adap = i2c_get_adapter(LR388K4_I2C_BUS);
	struct i2c_client irq_client = {
						.addr = LR388K4_I2C_SLAVE_ADDRESS, 	// slaveaddress : 7bit
						.adapter = adap,
						.irq = IRQ_MT,	
						.driver = &lr388k4_i2c_driver,
	};

	client->addr=irq_client.addr;
	client->adapter = irq_client.adapter;
	client->irq = irq_client.irq;
	client->driver = irq_client.driver;

	ts->min_x = LR388K4_I2C_X_MIN;
	ts->max_x = LR388K4_I2C_X_MAX;
	ts->min_y = LR388K4_I2C_Y_MIN;
	ts->max_y = LR388K4_I2C_Y_MAX;
	ts->pressure_max = 8000;
	ts->touch_num_max = 10;
	ts->flip_x = 0;
	ts->flip_y = 0;
	ts->swap_xy = 0;
	ts->irq0_pin = IRQ_MT;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA))
	{
		printk(KERN_ALERT "[SHARP_TOUCH]%s(%d):\n", __FILE__, __LINE__);
		return -EIO;
	}
	ts->client = client;
	
	snprintf(ts->phys, sizeof(ts->phys),"%s/input0", dev_name(&client->dev));

	input_dev->name = "LR388K4 touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_set_drvdata(input_dev, ts);

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	
	ts->input = input_dev;

	/* For multi-touch */
	err = input_mt_init_slots(input_dev, 20, INPUT_MT_DIRECT);  // max 10 fingers?

	if (err)
		goto err_free_mem;
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);
	input_set_abs_params(input_dev, ABS_X, LR388K4_I2C_X_MIN, LR388K4_I2C_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, LR388K4_I2C_Y_MIN, LR388K4_I2C_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, LR388K4_I2C_X_MIN, LR388K4_I2C_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, LR388K4_I2C_Y_MIN, LR388K4_I2C_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, LR388K4_I2C_P_MIN, LR388K4_I2C_P_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE , 0, MT_TOOL_MAX, 0, 0);
	
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, LR388K4_MAX_TOUCH_1PAGE, 0, 0);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	err = request_threaded_irq(client->irq, NULL, lr388k4_i2c_irq_thread, IRQF_TRIGGER_RISING|IRQF_ONESHOT, "touch_reset_key", ts);

	if (err < 0) {
		dev_err(&client->dev, "irq %d busy? error %d\n", client->irq, err);
		goto err_free_irq_gpio;
	}

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);
	device_init_wakeup(&client->dev, 1);
	printk(KERN_ALERT "[SHARP_TOUCH] probe() is done : %s(%d):\n", __FILE__, __LINE__);

	return 0;

err_free_irq:
	free_irq(client->irq, ts);
	printk(KERN_ALERT "[SHARP_TOUCH]err_free_irq : %s(%d):\n", __FILE__, __LINE__);
err_free_irq_gpio:
	//	gpio_free(ts->irq0_pin);
	//err_shutoff_device:
	printk(KERN_ALERT "[SHARP_TOUCH]err_free_irq_gpio : %s(%d):\n", __FILE__, __LINE__);
err_free_mem:
	printk(KERN_ALERT "[SHARP_TOUCH]err_free_mem : %s(%d):\n", __FILE__, __LINE__);
	input_free_device(input_dev);
	kfree(ts);
	return err;
}


static int lr388k4_i2c_remove(struct i2c_client *client)
{
	struct lr388k4_i2c *ts = i2c_get_clientdata(client);

	free_irq(client->irq, ts);
	input_unregister_device(ts->input);
	gpio_free(ts->irq0_pin);

	kfree(ts);

	return 0;
}

static int lr388k4_i2c_init(void)
{
	return i2c_add_driver(&lr388k4_i2c_driver);
}

static void lr388k4_i2c_exit(void)
{
	i2c_del_driver(&lr388k4_i2c_driver);
}

module_init(lr388k4_i2c_init);
module_exit(lr388k4_i2c_exit);

MODULE_DESCRIPTION("lr388k4_i2c TouchScreen Driver");
MODULE_LICENSE("GPL v2");
