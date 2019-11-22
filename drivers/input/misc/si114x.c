/* Silicon Labs 1141/42/43 Proximity/ALS Android Driver
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define SI114X_USE_INPUT_POLL 1

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>            /* kernel module definitions */
#include <linux/delay.h>
#include <linux/i2c.h>
#ifdef SI114X_USE_INPUT_POLL
#include <linux/input-polldev.h>
#else
#include <linux/input.h>
#endif  /* SI114X_USE_INPUT_POLL */
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <asm-generic/uaccess.h>
#include <linux/i2c/si114x.h>

#define DRIVER_VERSION		"0.2"
#define DEVICE_NAME			"si114x"
#define INPUT_DEVICE_NAME_PS			"proximity_sensor"
#define INPUT_DEVICE_NAME_ALS_IR		"infrared_sensor"
#define INPUT_DEVICE_NAME_ALS_VIS		"light_sensor"

#define VENDOR		"SILICON_LABS"
#define CHIP_ID		"SI114X"

#define SI114X_MAX_LED 		3
#define PS_THRESHOLD		512

enum {
	INFRARED_ENABLED = BIT(0),
	LIGHT_ENABLED = BIT(1),
	PROXIMITY_ENABLED = BIT(2),
};

enum proxi_node_state_event_t {
	PROXIMITY_STATE_NEAR = 0,
	PROXIMITY_STATE_FAR = 1,
	PROXIMITY_STATE_UNKNOWN = 2,
};

struct si114x_led {
	int drive_strength;
	int enable;
};

/* Both ALS and PS input device structures.  They could
 * be either interrupt driven or polled
 */
struct si114x_input_dev {
	struct input_dev *input;
#ifdef SI114X_USE_INPUT_POLL
	struct input_polled_dev *input_poll;
	unsigned int poll_interval;
#endif  /* SI114X_USE_INPUT_POLL */
};

struct si114x_data {
	/* Device */
	struct i2c_client *client;
	struct si114x_input_dev input_dev_als_ir;
	struct si114x_input_dev input_dev_als_vis;
	struct si114x_input_dev input_dev_ps;
	struct device proximity_dev;
	struct device als_ir_dev;
	struct device als_vis_dev;
	struct si114x_led led[SI114X_MAX_LED];
	const struct si114x_platform_data *pdata;
	/* Counter used for the command and response sync */
	int resp_id;
	int irq;
	uint8_t power_state;
	uint16_t ps_threshold;
};

/*
 * Filesystem API is implemented with the kernel fifo.
 */
#define SI114X_RD_QUEUE_SZ 256

static DECLARE_WAIT_QUEUE_HEAD(si114x_read_wait);

static struct si114x_data_user si114x_fifo_buffer[SI114X_RD_QUEUE_SZ];
static struct si114x_data_user *si114x_user_wr_ptr = NULL;
static struct si114x_data_user *si114x_user_rd_ptr = NULL;
static struct mutex si114x_user_lock;
static atomic_t si114x_opened = ATOMIC_INIT(0);
struct si114x_data *si114x_private = NULL;

/*
 * I2C bus transaction read for consecutive data.
 * Returns 0 on success.
 */
static int _i2c_read_mult(struct si114x_data *si114x, uint8_t *rxData, int length)
{
	int i;
	struct i2c_msg data[] = {
		{
			.addr = si114x->client->addr,
			.flags = 0,
			.len = 1,
			.buf = rxData,
		},
		{
			.addr = si114x->client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
		},
	};

	for (i = 0; i < I2C_RETRY; i++) {
		if (i2c_transfer(si114x->client->adapter, data, 2) > 0)
			break;

		/* Delay before retrying */
		dev_warn(&si114x->client->dev, "%s Retried read count:%d\n", __func__, i);

		mdelay(10);
	}

	if (i >= I2C_RETRY) {
		dev_err(&si114x->client->dev, "%s i2c read retry exceeded\n", __func__);
		return -EIO;
	}

	return 0;
}

/*
 * I2C bus transaction to write consecutive data.
 * Returns 0 on success.
 */
static int _i2c_write_mult(struct si114x_data *si114x, uint8_t *txData, int length)
{
	int i;
	struct i2c_msg data[] = {
		{
			.addr = si114x->client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	for (i = 0; i < I2C_RETRY; i++) {
		if (i2c_transfer(si114x->client->adapter, data, 1) > 0)
			break;

		/* Delay before retrying */
		dev_warn(&si114x->client->dev, "%s Retried read count:%d\n", __func__, i);

		mdelay(10);
	}

	if (i >= I2C_RETRY) {
		dev_err(&si114x->client->dev, "%s i2c write retry exceeded\n", __func__);
		return -EIO;
	}

	return 0;
}

/*
 * I2C bus transaction to read single byte.
 * Returns 0 on success.
 */
static int _i2c_read_one(struct si114x_data *si114x, int addr, int *data)
{
	int rc = 0;
	uint8_t buffer[1];

	buffer[0] = (uint8_t)addr;

	rc = _i2c_read_mult(si114x, buffer, sizeof(buffer));
	if (rc == 0) {
		*data = buffer[0];
	}

	return rc;
}

/*
 * I2C bus transaction to write single byte.
 * Returns 0 on success.
 */
static int _i2c_write_one(struct si114x_data *si114x, int addr, int data)
{
	uint8_t buffer[2];

	buffer[0] = (uint8_t)addr;
	buffer[1] = (uint8_t)data;

	return _i2c_write_mult(si114x, buffer, sizeof(buffer));
}

/*
 * Read and analyze the response after a write to the command register.
 * Returns 0 if response was ok.
 */
int _read_response(struct si114x_data *si114x) {
	int rc = 0;
	int data = 0;
	int wait_response;
	struct i2c_client *client = si114x->client;

	/* 
		for over 25 ms after the Command write, the entire Command process should be repeated
	*/		
	for (wait_response = 0; wait_response < 6; wait_response++) {
		rc = _i2c_read_one(si114x, SI114X_RESPONSE, &data);
		if (rc) {
			return rc;
		}

		if (data & SI114X_RESPONSE_ERR) {
			dev_err(&client->dev, "%s Response error value:0x%02x\n", __func__, data);
			return rc;
		}

		data &= SI114X_RESPONSE_ID;

		if (si114x->resp_id == data)
			break;

		msleep(5);
	}

	if (si114x->resp_id != data) {
		dev_err(&client->dev, "%s Unexpected response id act:%d exp:%d\n",
				__func__, data, si114x->resp_id);
	}

	si114x->resp_id = (si114x->resp_id + 1) & SI114X_RESPONSE_ID;

	dev_info(&client->dev, "%s Command id:%d", __func__, si114x->resp_id);

	return rc;
}

/*
 * Write a parameter and a command to the internal microprocessor on the ALS/PS sensor.
 * Read and verify response value.
 */
int _parm_write(struct si114x_data *si114x, int param_type, int param_addr, int param_data) {
	int rc = 0;
	uint8_t buffer[3];

	buffer[0] = SI114X_PARAM_WR;
	buffer[1] = param_data;
	buffer[2] = param_type | (param_addr & CMD_PARAM_ADDR_MASK);

	rc = _i2c_write_mult(si114x, buffer, sizeof(buffer));
	if (rc) {
		return rc;
	}

	return _read_response(si114x);
}

/* TODO(cmanton) If a NOP is sent then the response id must also be set to zero */
/*
 * Write a command to the internal microprocessor on the ALS/PS sensor
 * Read and verify response value.
 */
static int _cmd_write(struct si114x_data *si114x, int cmd) {
	int rc = 0;

	rc = _i2c_write_one(si114x, SI114X_COMMAND, cmd);
	if (rc) {
		return rc;
	}

	/* 	Reset Command because the device will reset itself and does not increment the Response
		register after reset.
	*/
	return _read_response(si114x);
}

static void si114x_input_report_als_values(struct input_dev *input, uint16_t *value)
{
	input_report_abs(input, ABS_MISC, value[0]);
	input_sync(input);
}

static void si114x_input_report_ps_values(struct input_dev *input, uint16_t *value)
{
	/* report by interrupt */
#if 0
	input_report_abs(input, ABS_DISTANCE, value[0]);
	input_report_abs(input, ABS_DISTANCE, value[1]);
	input_report_abs(input, ABS_DISTANCE, value[2]);

	input_sync(input);
#endif

	/* Now stick the values into the queue */
	si114x_user_wr_ptr->led_a = value[0];
	si114x_user_wr_ptr->led_b = value[1];
	si114x_user_wr_ptr->led_c = value[2];
	
	mutex_lock(&si114x_user_lock);

	si114x_user_wr_ptr++;
	if (si114x_user_wr_ptr == (si114x_fifo_buffer + SI114X_RD_QUEUE_SZ)) {
		si114x_user_wr_ptr = si114x_fifo_buffer;
	}

	mutex_unlock(&si114x_user_lock);

	wake_up_interruptible(&si114x_read_wait);
}

static int set_measurement_rates(struct si114x_data *si114x) {
	int rc = 0;

	rc += _i2c_write_one(si114x, SI114X_MEAS_RATE, SI114X_MEAS_RATE_10ms);
	rc += _i2c_write_one(si114x, SI114X_ALS_RATE, SI114X_ALS_RATE_1x);
	rc += _i2c_write_one(si114x, SI114X_PS_RATE, SI114X_PS_RATE_1x);

	return rc;
}

static int set_led_drive_strength(struct si114x_data *si114x)
{
	int rc = 0;

	rc += _i2c_write_one(si114x, SI114X_PS_LED21, si114x->led[1].drive_strength << 4
	                     | si114x->led[0].drive_strength);
	rc += _i2c_write_one(si114x, SI114X_PS_LED3, si114x->led[2].drive_strength);

	return rc;
}

/* Enable the channels for this device. */
static int enable_channels(struct si114x_data *si114x) 
{
	struct i2c_client *client = si114x->client;

	dev_dbg(&client->dev, "%s\n",	__func__);

	return _parm_write(si114x, CMD_PARAM_SET, PARAM_I2C_CHLIST,
	                   (CHLIST_EN_PS1 | CHLIST_EN_PS2 | CHLIST_EN_PS3
	                    | CHLIST_RSVD00 | CHLIST_EN_ALS_VIS
	                    | CHLIST_EN_ALS_IR));
}

static int disable_channels(struct si114x_data *si114x) {
	struct i2c_client *client = si114x->client;

	dev_dbg(&client->dev, "%s\n",	__func__);

	return _parm_write(si114x, CMD_PARAM_SET, PARAM_I2C_CHLIST, 0x0);
}


static int turn_on_leds(struct si114x_data *si114x) 
{
	struct i2c_client *client = si114x->client;
	int rc = 0;

	dev_dbg(&client->dev, "%s\n",	__func__);

	rc += _parm_write(si114x, CMD_PARAM_SET, PARAM_PSLED12_SELECT, 0x21);
	rc += _parm_write(si114x, CMD_PARAM_SET, PARAM_PSLED3_SELECT, 0x04);

	return rc;
}

static int turn_off_leds(struct si114x_data *si114x) 
{
	struct i2c_client *client = si114x->client;
	int rc = 0;

	dev_dbg(&client->dev, "%s\n",	__func__);

	rc += _parm_write(si114x, CMD_PARAM_SET, PARAM_PSLED12_SELECT, 0x00);
	rc += _parm_write(si114x, CMD_PARAM_SET, PARAM_PSLED3_SELECT, 0x00);

	return rc;
}

static int start_measurements(struct si114x_data *si114x) 
{
	struct i2c_client *client = si114x->client;
	
	dev_dbg(&client->dev, "%s\n",	__func__);

	return _cmd_write(si114x, CMD_PSALS_AUTO);
}

static int pause_measurements(struct si114x_data *si114x) 
{
	struct i2c_client *client = si114x->client;
	int rc = 0;
	
	dev_dbg(&client->dev, "%s\n",	__func__);

	rc += _cmd_write(si114x, CMD_PS_PAUSE);
	rc += _cmd_write(si114x, CMD_ALS_PAUSE);

	return rc;
}

static int set_ps_threshold(struct si114x_data *si114x, uint16_t ps_threshold) 
{
	struct i2c_client *client = si114x->client;
	int ps_thres = ps_threshold;
	int rc = 0;
	
	dev_dbg(&client->dev, "%s : ps_threshold:%d\n",	__func__, ps_thres);

	rc += _i2c_write_one(si114x, SI114X_PS1_TH_LOW, ps_thres & 0xFF);
	rc += _i2c_write_one(si114x, SI114X_PS1_TH_HIGH, (ps_thres >> 8) & 0xFF);	

	si114x->ps_threshold = ps_threshold;

	return rc;
}


#ifdef SI114X_USE_INPUT_POLL
/*
 * This callback is called by the input subsystem at the approrpriate polling
 * interval for retrieve the ALS infrared light sensor reading.
 */
static void si114x_input_poll_als_ir_cb(struct input_polled_dev *dev)
{
	uint8_t buffer[2];
	struct si114x_data *si114x = dev->private;
	struct i2c_client *client = si114x->client;
	struct input_dev *input;
	uint16_t *data16 = (uint16_t *)buffer;

	input = si114x->input_dev_als_ir.input_poll->input;

	/* Read sensor data from the IR light registers. */
	buffer[0] = SI114X_ALS_IR_DATA0;
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		dev_err(&client->dev, "%s: Unable to read als data\n", __func__);
		return;
	}

	si114x_input_report_als_values(input, data16);
}

/*
 * This callback is called by the input subsystem at the approrpriate polling
 * interval for retrieve the ALS visual light sensor reading.
 */
static void si114x_input_poll_als_vis_cb(struct input_polled_dev *dev)
{
	uint8_t buffer[2];
	struct si114x_data *si114x = dev->private;
	struct i2c_client *client = si114x->client;
	struct input_dev *input;
	uint16_t *data16 = (uint16_t *)buffer;

	input = si114x->input_dev_als_vis.input_poll->input;

	/* Read sensor data from the visible light registers. */
	buffer[0] = SI114X_ALS_VIS_DATA0;
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		dev_err(&client->dev, "%s: Unable to read als data\n", __func__);
		return;
	}

	si114x_input_report_als_values(input, data16);
}

/*
 * This callback is called by the input subsystem at the approrpriate polling
 * interval.
 */
static void si114x_input_poll_ps_cb(struct input_polled_dev *dev)
{
	uint8_t buffer[6];
	struct si114x_data *si114x = dev->private;
	struct i2c_client *client = si114x->client;
	struct input_dev *input;
	uint16_t *data16 = (uint16_t *)buffer;

	input = si114x->input_dev_ps.input_poll->input;

	buffer[0] = SI114X_PS1_DATA0;

	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		dev_err(&client->dev, "%s: Unable to read ps data\n", __func__);
		return;
	}

	// dev_err(&client->dev, "%s: Got callback %d %d %d\n", __func__, data16[0], data16[1], data16[2]);
	si114x_input_report_ps_values(input, data16);
}

/* Call from the input subsystem to start polling the device */
static int si114x_input_als_ir_open(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return 0;
}

/* Call from the input subsystem to stop polling the device */
static void si114x_input_als_ir_close(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return;
}

/* Call from the input subsystem to start polling the device */
static int si114x_input_als_vis_open(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return 0;
}

/* Call from the input subsystem to stop polling the device */
static void si114x_input_als_vis_close(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return;
}

/* Create and register als infrared light polled input mechanism */
static int setup_als_ir_polled_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_polled_dev *input_poll;
	struct input_dev *input;
	
	input_poll = input_allocate_polled_device();
	if (!input_poll) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}

	input_poll->private = si114x;
	input_poll->poll = si114x_input_poll_als_ir_cb;
	input_poll->poll_interval = si114x->pdata->als_poll_interval;

	input = input_poll->input;

	input->name = INPUT_DEVICE_NAME_ALS_IR;
	input->open = si114x_input_als_ir_open;
	input->close = si114x_input_als_ir_close;
	set_bit(EV_ABS, input->evbit);
	input->id.bustype = BUS_I2C;
	input->dev.parent = &si114x->client->dev;

	input_set_abs_params(input, ABS_MISC, ALS_MIN_MEASURE_VAL, ALS_MAX_MEASURE_VAL+1, 0, 0);

	input_set_drvdata(input, input_poll);

	rc = input_register_polled_device(input_poll);
	if (rc) {
		dev_err(&si114x->client->dev,
		        "Unable to register input polled device %s\n",
		        input->name);
		goto err_als_register_input_device;
	}
	
	si114x->input_dev_als_ir.input = input;
	si114x->input_dev_als_ir.input_poll = input_poll;
	
	return rc;
	
 err_als_register_input_device:
	input_free_polled_device(input_poll);
	
	return rc;
}

/* Create and register als visible light polled input mechanism */
static int setup_als_vis_polled_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_polled_dev *input_poll;
	struct input_dev *input;

	input_poll = input_allocate_polled_device();

	if (!input_poll) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}

	input_poll->private = si114x;
	input_poll->poll = si114x_input_poll_als_vis_cb;
	input_poll->poll_interval = si114x->pdata->als_poll_interval;

	input = input_poll->input;
	
	input->name = INPUT_DEVICE_NAME_ALS_VIS;
	input->open = si114x_input_als_vis_open;
	input->close = si114x_input_als_vis_close;
	set_bit(EV_ABS, input->evbit);
	input->id.bustype = BUS_I2C;
	input->dev.parent = &si114x->client->dev;

	input_set_abs_params(input, ABS_MISC, ALS_MIN_MEASURE_VAL, ALS_MAX_MEASURE_VAL+1, 0, 0);
	input_set_drvdata(input, input_poll);

	rc = input_register_polled_device(input_poll);
	if (rc) {
		dev_err(&si114x->client->dev,
		        "Unable to register input polled device %s\n",
		        input->name);
		goto err_als_register_input_device;
	}

	si114x->input_dev_als_vis.input = input;
	si114x->input_dev_als_vis.input_poll = input_poll;

	return rc;

 err_als_register_input_device:
	input_free_polled_device(input_poll);

	return rc;
}

/* Call from the input subsystem to start polling the device */
static int si114x_input_ps_open(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return 0;
}

/* Call from the input subsystem to stop polling the device */
static void si114x_input_ps_close(struct input_dev *input)
{
	struct si114x_data *si114x = input_get_drvdata(input);
	struct i2c_client *client = si114x->client;

	dev_info(&client->dev, "%s\n", __func__);

	return;
}

static int setup_ps_polled_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_polled_dev *input_poll;
	struct input_dev *input;

	input_poll = input_allocate_polled_device();
	if (!input_poll) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}

	input_poll->private = si114x;
	input_poll->poll = si114x_input_poll_ps_cb;
	input_poll->poll_interval = si114x->pdata->ps_poll_interval;

	input = input_poll->input;

	input->name = INPUT_DEVICE_NAME_PS;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &si114x->client->dev;
	set_bit(EV_ABS, input->evbit);
	input->open = si114x_input_ps_open;
	input->close = si114x_input_ps_close;
	
	input_set_abs_params(input, ABS_DISTANCE, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);
	input_set_abs_params(input, ABS_DISTANCE, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);
	input_set_abs_params(input, ABS_DISTANCE, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);

	/* max is a signed value */
	input_set_abs_params(input, ABS_DISTANCE, 0, 0x7fffffff, 0, 0);
	input_set_drvdata(input, input_poll);
	
	rc = input_register_polled_device(input_poll);
	if (rc) {
		dev_err(&si114x->client->dev,
		        "Unable to register input polled device %s\n",
		        input_poll->input->name);
		goto err_ps_register_input_device;
	}
	
	dev_dbg(&client->dev, "%s Registered input polling device rate:%d\n",
	        __func__, input_poll->poll_interval);
	        
	si114x->input_dev_ps.input = input;
	si114x->input_dev_ps.input_poll = input_poll;

	return rc;
	
err_ps_register_input_device:
	input_free_polled_device(si114x->input_dev_ps.input_poll);
	
	return rc;
}

#else

/* Create and register interrupt driven input mechanism */
static int setup_als_ir_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_dev *input_dev;
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}
	input_dev->name = INPUT_DEVICE_NAME_ALS_IR;
	
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_MISC, ALS_MIN_MEASURE_VAL, ALS_MAX_MEASURE_VAL+1, 0, 0);
	
	rc = input_register_device(input_dev);
	if (rc < 0) {
		dev_err(&client->dev, "%s: Unable to register input device\n", __func__);
		goto err_als_register_input_device;
	}
	
	si114x->input_dev_als_ir.input = input_dev;
	
	return rc;
	
 err_als_register_input_device:
	input_free_device(input_dev);
	
	return rc;
}

/* Create and register interrupt driven input mechanism */
static int setup_als_vis_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_dev *input_dev;
	
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}
	
	input_dev->name = INPUT_DEVICE_NAME_ALS_VIS;
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_MISC, ALS_MIN_MEASURE_VAL, ALS_MAX_MEASURE_VAL+1, 0, 0);
	
	rc = input_register_device(input_dev);
	if (rc < 0) {
		dev_err(&client->dev, "%s: Unable to register input device\n", __func__);
		goto err_als_register_input_device;
	}
	
	si114x->input_dev_als_vis.input = input_dev;
	
	return rc;
	
 err_als_register_input_device:
	input_free_device(input_dev);
	
	return rc;
}

static int setup_ps_input(struct si114x_data *si114x)
{
	int rc;
	struct i2c_client *client = si114x->client;
	struct input_dev *input_dev;
	
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "%s: Unable to allocate input device\n", __func__);
		return -ENOMEM;
	}
	
	input_dev->name = INPUT_DEVICE_NAME_PS;
	
	set_bit(EV_ABS, input_dev->evbit);
	
	input_set_abs_params(input, ABS_X, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);
	input_set_abs_params(input, ABS_Y, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);
	input_set_abs_params(input, ABS_Z, PS_MIN_MEASURE_VAL, PS_MAX_MEASURE_VAL, 0, 0);
	
	rc = input_register_device(input_dev);
	if (rc < 0) {
		dev_err(&client->dev, "%s: PS Register Input Device Fail...\n", __func__);
		goto err_ps_register_input_device;
	}

	si114x->input_dev_ps.input = input_dev;
	
	return rc;

err_ps_register_input_device:
	input_free_device(input_dev);

	return rc;
}
#endif  /* SI114X_USE_INPUT_POLL */

static ssize_t psals_data_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
	uint8_t buffer[sizeof(uint16_t)*5];
	struct si114x_data *si114x;
	uint16_t *data16 = (uint16_t *)buffer;
	si114x = dev_get_drvdata(dev);
	
	if (!si114x) {
		return -ENODEV;
	}
	
	buffer[0] = SI114X_ALS_VIS_DATA0;
	
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		return -EIO;
	}
	
	return sprintf(buf, "%u %u %u %u %u\n", data16[0], data16[1],
	               data16[2], data16[3], data16[4]);
}

static DEVICE_ATTR(psals_data, 0444, psals_data_show, NULL);

static ssize_t psals_data_show_hdr(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x;
	si114x = dev_get_drvdata(dev);
	if (!si114x) {
		return -ENODEV;
	}
	return sprintf(buf, "als_vis als_ir ps1 ps2 ps3\n");
}

static DEVICE_ATTR(psals_data_hdr, 0444, psals_data_show_hdr, NULL);

static ssize_t als_ir_data_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
	uint8_t buffer[sizeof(uint16_t)*1];
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	uint16_t *data16 = (uint16_t *)buffer;
	
	input_poll = dev_get_drvdata(dev);
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}
	
	buffer[0] = SI114X_ALS_IR_DATA0;
	
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		return -EIO;
	}
	
       return sprintf(buf, "%u\n", data16[0]);
}

static DEVICE_ATTR(als_ir_data, 0444, als_ir_data_show, NULL);

static ssize_t als_vis_data_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
	uint8_t buffer[sizeof(uint16_t)*1];
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	uint16_t *data16 = (uint16_t *)buffer;
	
	input_poll = dev_get_drvdata(dev);
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}
	
	buffer[0] = SI114X_ALS_VIS_DATA0;
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		return -EIO;
	}
	
       return sprintf(buf, "%u\n", data16[0]);
}

static DEVICE_ATTR(als_vis_data, 0444, als_vis_data_show, NULL);

static ssize_t als_vis_gain_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	int data = 0;
	
	input_poll = dev_get_drvdata(dev);
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}
	
	if (_cmd_write(si114x,  CMD_PARAM_QUERY
	               | ( PARAM_ALS_VIS_ADC_GAIN & PARAM_MASK))) {
		return -EIO;
	}
	
	/* Wait for the data to present. */
	msleep(1);

	_i2c_read_one(si114x, SI114X_PARAM_RD, &data);

	return sprintf(buf, "%d", data);
}
static ssize_t als_vis_gain_store(struct device *dev,
                                  struct device_attribute *attr, const char *buf,
                                  size_t count)
{
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	int data;
	input_poll = dev_get_drvdata(dev);
	
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}
	
	sscanf(buf, "%d", &data);
	
	/* Validate parameters */
	if (data < 0 || data > 15) {
		return -EINVAL;
	}
	
	/* Set the irLED pulse width and integration time. */
	if (_parm_write(si114x, CMD_PARAM_SET, PARAM_ALS_VIS_ADC_GAIN, data)) {
		return -EIO;
	}
	
	return count;
}

static DEVICE_ATTR(als_vis_gain, 0666, als_vis_gain_show, als_vis_gain_store);

static ssize_t ps_data_show(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
	uint8_t buffer[sizeof(uint16_t)*3];
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	uint16_t *data16 = (uint16_t *)buffer;
	struct timespec ts;

	ktime_get_ts(&ts);
	
	input_poll = dev_get_drvdata(dev);
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}
	
	buffer[0] = SI114X_PS1_DATA0;
	if (_i2c_read_mult(si114x, buffer, sizeof(buffer))) {
		return -EIO;
	}
	
       return sprintf(buf, "%u %u %u %lu\n", data16[0], data16[1], data16[2], ts.tv_nsec);
}

static DEVICE_ATTR(ps_input_data, 0444, ps_data_show, NULL);

static ssize_t led_drive_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x;
	si114x = dev_get_drvdata(dev);
	
	if (!si114x) {
		return -ENODEV;
	}
	
	return sprintf(buf, "%u %u %u\n", si114x->led[0].drive_strength,
	               si114x->led[1].drive_strength,
	               si114x->led[2].drive_strength);
}
static ssize_t led_drive_store(struct device *dev, struct device_attribute *attr,
                               const char *buf, size_t count)
{
	int i;
	struct si114x_data *si114x;
	unsigned int drive_strength[SI114X_MAX_LED];
	
	si114x = dev_get_drvdata(dev);
	
	sscanf(buf, "%d %d %d", &drive_strength[0], &drive_strength[1], &drive_strength[2]);
	
	/* Validate parameters */
	for (i = 0; i < SI114X_MAX_LED; i++) {
		if (drive_strength[i] > 15) {
			return -EINVAL;
		}
	}

	/* Update local copies of parameters */
	for (i = 0; i < SI114X_MAX_LED; i++) {
		si114x->led[i].drive_strength = drive_strength[i];
	}

	/* Write parameters to device */
	set_led_drive_strength(si114x);

	return count;
}

static DEVICE_ATTR(led_drive, 0666, led_drive_show, led_drive_store);

static ssize_t led_input_drive_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	input_poll = dev_get_drvdata(dev);

	if (!input_poll) {
		return -ENODEV;
	}

	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}

	return sprintf(buf, "%u %u %u\n", si114x->led[0].drive_strength,
	               si114x->led[1].drive_strength,
	               si114x->led[2].drive_strength);
}

static ssize_t led_input_drive_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
	int i;
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;
	unsigned int drive_strength[SI114X_MAX_LED];

	input_poll = dev_get_drvdata(dev);
	if (!input_poll) {
		return -ENODEV;
	}

	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}

	sscanf(buf, "%d %d %d", &drive_strength[0], &drive_strength[1], &drive_strength[2]);

	/* Validate parameters */
	for (i = 0; i < SI114X_MAX_LED; i++) {
		if (drive_strength[i] > 15) {
			return -EINVAL;
		}
	}

	/* Update local copies of parameters */
	for (i = 0; i < SI114X_MAX_LED; i++) {
		si114x->led[i].drive_strength = drive_strength[i];
	}

	/* Write parameters to device */
	set_led_drive_strength(si114x);

	return count;
}

static DEVICE_ATTR(led_input_drive, 0644, led_input_drive_show, led_input_drive_store);


/*
 * Dump the parameter ram contents.
 */
static ssize_t param_data_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
	int i;
	int data = 0;
	char *pbuf = buf;
	struct si114x_data *si114x;
	
	si114x = dev_get_drvdata(dev);
	
	for (i = 0; i < PARAM_MAX; i++) {
		if (i % 8 == 0) {
			pbuf += sprintf(pbuf, "%02x: ", i);
		}

		if (_cmd_write(si114x,  CMD_PARAM_QUERY | ( i & PARAM_MASK))) {
			return -1;
		}

		/* Wait for the data to present. */
		msleep(1);

		_i2c_read_one(si114x, SI114X_PARAM_RD, &data);

		pbuf += sprintf(pbuf, "%02x ", data);

		if ((i + 1) % 8 == 0) {
			pbuf += sprintf(pbuf, "\n");
		}
	}
	return strlen(buf);
}

static DEVICE_ATTR(param_data, 0440, param_data_show, NULL);

static ssize_t vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VENDOR);
}

static DEVICE_ATTR(vendor, 0444, vendor_show, NULL);

static ssize_t name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CHIP_ID);
}

static DEVICE_ATTR(name, 0444, name_show, NULL);

static ssize_t ps_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	bool new_value;
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;

	input_poll = dev_get_drvdata(dev);
	
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	if (new_value == true  && !(si114x->power_state & PROXIMITY_ENABLED)) {
		si114x->power_state |= PROXIMITY_ENABLED;
		
		turn_on_leds(si114x);
		enable_channels(si114x);
		start_measurements(si114x);

		enable_irq(si114x->irq);
	} else if (new_value == false && (si114x->power_state & PROXIMITY_ENABLED)) {
		si114x->power_state &= ~PROXIMITY_ENABLED;

		disable_irq(si114x->irq);
		
		disable_channels(si114x);
		turn_off_leds(si114x);
		pause_measurements(si114x);
	}

	return size;
}

static ssize_t ps_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x;
	struct input_polled_dev *input_poll;

	input_poll = dev_get_drvdata(dev);
	
	if (!input_poll) {
		return -ENODEV;
	}
	
	si114x = (struct si114x_data *)input_poll->private;
	if (!si114x) {
		return -ENODEV;
	}

	return sprintf(buf, "%d\n",
		       (si114x->power_state & PROXIMITY_ENABLED) ? 1 : 0);
}

static DEVICE_ATTR(enable, 0644, ps_enable_show, ps_enable_store);

static ssize_t ps_thresh_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct si114x_data *si114x = dev_get_drvdata(dev);
	
	return sprintf(buf, "ps_threshold = %d\n", si114x->ps_threshold);
}

static ssize_t ps_thresh_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct si114x_data *si114x = dev_get_drvdata(dev);
	uint16_t thresh_value = PS_THRESHOLD;
	int err = 0;

	err = kstrtou16(buf, 10, &thresh_value);
	if (err < 0)
		pr_err("%s, kstrto16 failed.", __func__);

	set_ps_threshold(si114x, thresh_value);

	return size;
}

static DEVICE_ATTR(ps_thresh, 0644, ps_thresh_show,
	ps_thresh_store);


static struct attribute *ps_sysfs_attrs[] = {
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group ps_attribute_group = {
	.attrs = ps_sysfs_attrs,
};

static ssize_t als_ir_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{

	return size;
}

static ssize_t als_ir_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return 0;
}

static DEVICE_ATTR(als_ir_enable, 0666, als_ir_enable_show, als_ir_enable_store);

static struct attribute *als_ir_sysfs_attrs[] = {
	&dev_attr_als_ir_enable.attr,
	NULL
};

static struct attribute_group als_ir_attribute_group = {
	.attrs = als_ir_sysfs_attrs,
};

static ssize_t als_vis_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{

	return size;
}

static ssize_t als_vis_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return 0;
}

static DEVICE_ATTR(als_vis_enable, 0666, als_vis_enable_show, als_vis_enable_store);

static struct attribute *als_vis_sysfs_attrs[] = {
	&dev_attr_als_vis_enable.attr,
	NULL
};

static struct attribute_group als_vis_attribute_group = {
	.attrs = als_vis_sysfs_attrs,
};


/*
 * These sysfs routines are not used by the Android HAL layer
 * as they are located in the i2c bus device portion of the
 * sysfs tree.
 */
static void sysfs_register_bus_entry(struct si114x_data *si114x, struct i2c_client *client) {
	int rc = 0;

	/* Store the driver data into our device private structure */
	dev_set_drvdata(&client->dev, si114x);

	rc += device_create_file(&client->dev, &dev_attr_psals_data);
	rc += device_create_file(&client->dev, &dev_attr_psals_data_hdr);
	rc += device_create_file(&client->dev, &dev_attr_led_drive);
	rc += device_create_file(&client->dev, &dev_attr_param_data);

	if (rc) {
		dev_err(&client->dev, "%s Unable to create sysfs bus files\n", __func__);
	} else {
		dev_dbg(&client->dev, "%s Created sysfs bus files\n", __func__);
	}
}

#define CLASS_NAME	"sensors"

static struct class *sensor_class;

static char *sensor_devnode(struct device *dev, umode_t *mode)
{
	*mode = 0666; /* rw-rw-rw- */
	return NULL;
}

/*
 * These sysfs routines are exposed to the Android HAL layer as they are
 * created in the class/input portion of the sysfs tree.
 */
static void sysfs_register_class_input_entry_ps(struct si114x_data *si114x, struct device *dev) {
	int rc = 0;
	struct device *prox_dev = &si114x->proximity_dev;
	struct i2c_client *client = si114x->client;

	rc = sysfs_create_group(&dev->kobj,
				 &ps_attribute_group);
	if (rc ) {
		dev_err(&client->dev, "%s: could not create sysfs group\n", __func__);
	}

	/* create class. (/sys/class/sensors) */
	if (!sensor_class) {
		sensor_class = class_create(THIS_MODULE, CLASS_NAME);
		if (IS_ERR(sensor_class)) {
			rc = PTR_ERR(sensor_class);
			dev_err(&client->dev, "%s Unable to create sensors class files\n", __func__);
			goto out;
		}

		sensor_class->devnode = sensor_devnode;
	}
	
	/* create class device. (/sys/class/sdp_regctrl/sdp_regctrl) */
	prox_dev = device_create(sensor_class, NULL, dev->devt, si114x->input_dev_ps.input_poll,
								INPUT_DEVICE_NAME_PS);
	if (IS_ERR(prox_dev)) {
		rc = PTR_ERR(prox_dev);
		goto out_unreg_class;
	}

	rc += device_create_file(prox_dev, &dev_attr_ps_input_data);
	rc += device_create_file(prox_dev, &dev_attr_led_input_drive);
	rc += device_create_file(prox_dev, &dev_attr_ps_thresh);	
	rc += device_create_file(prox_dev, &dev_attr_vendor);
	rc += device_create_file(prox_dev, &dev_attr_name);
	
	if (rc) {
		dev_err(&client->dev, "%s Unable to create sysfs class files\n", __func__);
	} else {
		dev_dbg(&client->dev, "%s Created sysfs class files\n", __func__);
	}
	
	return ;

out_unreg_class:
	class_destroy(sensor_class);
	sensor_class = NULL;

out:
	return ;
}

static void sysfs_register_class_input_entry_als_ir(struct si114x_data *si114x, struct device *dev) {
	int rc = 0;
	struct device *als_ir_dev = &si114x->als_ir_dev;
	struct i2c_client *client = si114x->client;

	rc = sysfs_create_group(&dev->kobj,
				 &als_ir_attribute_group);
	if (rc ) {
		dev_err(&client->dev, "%s: could not create sysfs group\n", __func__);
	}

	/* create class. (/sys/class/sensors) */
	if (!sensor_class) {
		sensor_class = class_create(THIS_MODULE, CLASS_NAME);
		if (IS_ERR(sensor_class)) {
			rc = PTR_ERR(sensor_class);
			dev_err(&client->dev, "%s Unable to create sensors class files\n", __func__);
			goto out;
		}

		sensor_class->devnode = sensor_devnode;
	}
	
	/* create class device.*/
	als_ir_dev = device_create(sensor_class, NULL, dev->devt, si114x->input_dev_als_ir.input_poll,
								INPUT_DEVICE_NAME_ALS_IR);
	if (IS_ERR(als_ir_dev)) {
		rc = PTR_ERR(als_ir_dev);
		goto out_unreg_class;
	}
	
	rc += device_create_file(als_ir_dev, &dev_attr_als_ir_data);
	rc += device_create_file(als_ir_dev, &dev_attr_led_input_drive);
	if (rc) {
		dev_err(&client->dev, "%s Unable to create sysfs class files\n", __func__);
	} else {
		dev_dbg(&client->dev, "%s Created sysfs class files\n", __func__);
	}
	
	return;
	
out_unreg_class:
	class_destroy(sensor_class);
	sensor_class = NULL;

out:
	return ;	
}
static void sysfs_register_class_input_entry_als_vis(struct si114x_data *si114x, struct device *dev) {
	int rc = 0;
	struct device *als_vis_dev = &si114x->als_vis_dev;
	struct i2c_client *client = si114x->client;

	rc = sysfs_create_group(&dev->kobj,
				 &als_vis_attribute_group);
	if (rc ) {
		dev_err(&client->dev, "%s: could not create sysfs group\n", __func__);
	}

	/* create class. (/sys/class/sensors) */
	if (!sensor_class) {
		sensor_class = class_create(THIS_MODULE, CLASS_NAME);
		if (IS_ERR(sensor_class)) {
			rc = PTR_ERR(sensor_class);
			dev_err(&client->dev, "%s Unable to create sensors class files\n", __func__);
			goto out;
		}

		sensor_class->devnode = sensor_devnode;
	}
	
	/* create class device.*/
	als_vis_dev = device_create(sensor_class, NULL, dev->devt, si114x->input_dev_als_vis.input_poll,
								INPUT_DEVICE_NAME_ALS_VIS);
	if (IS_ERR(als_vis_dev)) {
		rc = PTR_ERR(als_vis_dev);
		goto out_unreg_class;
	}
	
	rc += device_create_file(als_vis_dev, &dev_attr_als_vis_data);
	rc += device_create_file(als_vis_dev, &dev_attr_als_vis_gain);
	rc += device_create_file(als_vis_dev, &dev_attr_led_input_drive);
	if (rc) {
		dev_err(&client->dev, "%s Unable to create sysfs class files\n", __func__);
	} else {
		dev_dbg(&client->dev, "%s Created sysfs class files\n", __func__);
	}
	
	return ;
	
out_unreg_class:
	class_destroy(sensor_class);
	sensor_class = NULL;

out:
	return ;		
}

static int _check_id(struct si114x_data *si114x)
{
	int rc = 0;
	int part_id = 0;
	int seq_id = 0;
	struct i2c_client *client = si114x->client;

	rc = _i2c_read_one(si114x, SI114X_PART_ID, &part_id);
	if (rc < 0) {
		dev_err(&client->dev, "%s: Unable to read part identifier\n",
		        __func__);
		return -EIO;
	}

	rc = _i2c_read_one(si114x, SI114X_SEQ_ID, &seq_id);
	if (rc < 0) {
		dev_err(&client->dev, "%s: Unable to read sequencer revision identifier\n",
				__func__);
		return -EIO;
	}

	switch (part_id) {
		case PART_ID_SI1141:
			dev_err(&client->dev, "%s: Detected SI1141 part_id:%02x, seq_id:%02x\n", __func__, part_id, seq_id);
			break;
		case PART_ID_SI1142:
			dev_err(&client->dev, "%s: Detected SI1142 part_id:%02x, seq_id:%02x\n", __func__, part_id, seq_id);
			break;
		case PART_ID_SI1143:
			dev_err(&client->dev, "%s: Detected SI1143 part_id:%02x, seq_id:%02x\n", __func__, part_id, seq_id);
			break;
		default:
			dev_err(&client->dev, "%s: Unable to determine SI114x part_id:%02x, seq_id:%02x\n", __func__, part_id, seq_id);
			rc = -ENODEV;
	}
	
	return rc;
}

/* PS open fops */
static int si114x_open(struct inode *inode, struct file *file)
{
	struct i2c_client *client;
	if (atomic_xchg(&si114x_opened, 1) == 1) {
		return -EBUSY;
	}
	file->private_data = (void*)si114x_private;
	client = si114x_private->client;
	/* Set the read pointer equal to the write pointer */
	if (mutex_lock_interruptible(&si114x_user_lock)) {
		dev_err(&client->dev, "%s: Unable to set read pointer\n", __func__);
		return -EAGAIN;
	}
	si114x_user_rd_ptr = si114x_user_wr_ptr;
	mutex_unlock(&si114x_user_lock);
        client = si114x_private->client;
	dev_info(&client->dev, "%s: Opened device\n", __func__);
	dev_info(&client->dev, "%p %s\n", inode, inode->i_sb->s_id);
	return 0;
}
/* PS release fops */
static int si114x_release(struct inode *inode, struct file *file)
{
	struct si114x_data *si114x = (struct si114x_data *)file->private_data;
	struct i2c_client *client = si114x->client;
	if (atomic_xchg(&si114x_opened, 0) == 0) {
		dev_err(&client->dev, "%s: Device has not been opened\n", __func__);
		return -EBUSY;
	}
	dev_info(&client->dev, "%s: Closed device\n", __func__);
	return 0;
}

/* PS IOCTL */
static long si114x_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOSYS;
}

static ssize_t si114x_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int rc = 0;
	struct si114x_data *si114x = (struct si114x_data *)file->private_data;
	struct i2c_client *client = si114x->client;

	if (count == 0)
		return 0;
		
	if (count < sizeof(struct si114x_data_user))
		return -EINVAL;
		
	if (count > SI114X_RD_QUEUE_SZ * sizeof(struct si114x_data_user))
		count = SI114X_RD_QUEUE_SZ * sizeof(struct si114x_data_user);

	if (mutex_lock_interruptible(&si114x_user_lock)) {
	        dev_err(&client->dev, "%s: Unable to acquire user read lock\n", __func__);
		return -EAGAIN;
	}
	
	if (si114x_user_rd_ptr == si114x_user_wr_ptr) {
		mutex_unlock(&si114x_user_lock);
		
		if (file->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		}

		wait_event_interruptible(si114x_read_wait,
		                         si114x_user_rd_ptr != si114x_user_wr_ptr);

		if (mutex_lock_interruptible(&si114x_user_lock)) {
			dev_err(&client->dev, "%s: Unable to acquire user read lock after sleep\n", __func__);
			return -EAGAIN;
		}
	}

	while (si114x_user_rd_ptr != si114x_user_wr_ptr && count >= sizeof(struct si114x_data_user)) {
		if (copy_to_user(buf, si114x_user_rd_ptr,
		                 sizeof(struct si114x_data_user))) {
			rc = -EFAULT;
			break;
		}

		rc += sizeof(struct si114x_data_user);
		buf += sizeof(struct si114x_data_user);
		count -= sizeof(struct si114x_data_user);
		
		si114x_user_rd_ptr++;
		if (si114x_user_rd_ptr == (si114x_fifo_buffer + SI114X_RD_QUEUE_SZ)) {
			si114x_user_rd_ptr = si114x_fifo_buffer;
		}
	}
	
	mutex_unlock(&si114x_user_lock);
	
	return rc;
}

static const struct file_operations si114x_fops = {
	.owner = THIS_MODULE,
	.open = si114x_open,
	.release = si114x_release,
	.unlocked_ioctl = si114x_ioctl,
	.read = si114x_read,
};

/* interrupt happened due to transition/change of near/far proximity state */
irqreturn_t si114x_irq_thread_fn(int irq, void *data)
{
	uint8_t buffer[6] = {0, };
	struct si114x_data *si114x = data;
	struct i2c_client *client = si114x->client;
	struct input_dev *input;
	uint16_t *data16 = (uint16_t *)buffer;
	int val = PROXIMITY_STATE_FAR;

	input = si114x->input_dev_ps.input_poll->input;

	buffer[0] = SI114X_PS1_DATA0;

	_i2c_read_mult(si114x, buffer, sizeof(buffer));

	if (data16[0] > si114x->ps_threshold)
		val = PROXIMITY_STATE_NEAR;
	else
		val = PROXIMITY_STATE_FAR;

	/* 0 is close, 1 is far */
	input_report_abs(input, ABS_DISTANCE, val);
	input_sync(input);

	dev_info(&client->dev, "%s: val = %d, ps_data = %d (close:0, far:1)\n",
			__func__, val, data16[0]);

	return IRQ_HANDLED;
}


struct miscdevice si114x_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = INPUT_DEVICE_NAME_PS,
	.fops = &si114x_fops
};

static int si114x_setup(struct si114x_data *si114x) {
	int rc = 0;
	
	/* Set the proper operating hardware key */
	_i2c_write_one(si114x, SI114X_HW_KEY, HW_KEY);
	
	set_measurement_rates(si114x);
	
	set_led_drive_strength(si114x);
	
	/* Set the PS sensitivity and measurement mode. */
	_parm_write(si114x, CMD_PARAM_SET, PARAM_ADC_MISC, 0x24);

	/* Set the irLED pulse width and integration time. */
	_parm_write(si114x, CMD_PARAM_SET, PARAM_ALS_VIS_ADC_GAIN, 0x05);

	set_ps_threshold(si114x, PS_THRESHOLD);

	/* Set PS1 interrupt enable */
	_i2c_write_one(si114x, SI114X_IRQ_ENABLE, 0x04);
	/* PS1_INT is set whenever the current PS1 measurement crosses the PS1_TH
 		threshold */
	_i2c_write_one(si114x, SI114X_IRQ_MODE1, 0x10);

	return rc;
}

static int si114x_setup_irq(struct si114x_data *si114x)
{
#if 0
	int rc = -EIO;
	struct si114x_platform_data *pdata = si114x->pdata;

	rc = gpio_request(pdata->gpio_int_no, "gpio_proximity_out");
	if (rc < 0) {
		pr_err("%s: gpio %d request failed (%d)\n",
		       __func__, pdata->gpio_int_no, rc);
		return rc;
	}

	rc = gpio_direction_input(pdata->irq);
	if (rc < 0) {
		pr_err("%s: failed to set gpio %d as input (%d)\n",
		       __func__, pdata->gpio_int_no, rc);
		goto err_gpio_direction_input;
	}

	si114x->irq = gpio_to_irq(pdata->irq);
	rc = request_threaded_irq(si114x->irq, NULL,
				  si114x_irq_thread_fn,
				  IRQF_DISABLED | IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				  "proximity_int", si114x);
	if (rc < 0) {
		pr_err("%s: request_irq(%d) failed for gpio %d (%d)\n",
		       __func__, si114x->irq, pdata->gpio_int_no, rc);
		goto err_request_irq;
	}

	/* start with interrupts disabled */
	disable_irq(si114x->irq);

	pr_err("%s, success\n", __func__);

	goto done;

err_request_irq:
err_gpio_direction_input:
	gpio_free(pdata->irq);
done:
	return rc;
#else
	return 0;
#endif
}


static struct si114x_platform_data si114x_pfd = {
	.gpio_int_no = 0,
	.meas_rate = 0,
	.als_rate = 0,
	.ps_rate = 0,
	.ps_led1 = 0,
	.ps_led2 = 0,
	.ps_led3 = 0,
	.als_poll_interval = 0,
	.ps_poll_interval = 2000
};

static int  si114x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct si114x_data *si114x = NULL;

	si114x = kzalloc(sizeof(struct si114x_data), GFP_KERNEL);

	if (!si114x)
	{
		dev_err(&client->dev, "%s: Unable to allocate memory for driver structure\n", __func__);
		return -ENOMEM;
	}
	
	si114x->client = client;
	
	dev_err(&client->dev, "%s: Probing si114x..\n", __func__);

	if (_check_id(si114x)) {
		goto err_out;
	}
	
	/* Reset the device 
		No Commands should be issued to the device for at least 1 ms after a Reset is issued
	*/
	_cmd_write(si114x, CMD_RESET);

	msleep(20);
	
	/* Set platform defaults */
	si114x->pdata = (const struct si114x_platform_data *)&si114x_pfd;
	if (!si114x->pdata) {
		dev_err(&client->dev, "%s: Platform data has not been set\n", __func__);
		goto err_out;
	}
	
	si114x->led[0].drive_strength = si114x->pdata->ps_led1;
	if (si114x->led[0].drive_strength)
		si114x->led[0].enable = 1;
		
	si114x->led[1].drive_strength = si114x->pdata->ps_led2;
	if (si114x->led[1].drive_strength)
		si114x->led[1].enable = 1;
		
	si114x->led[2].drive_strength = si114x->pdata->ps_led3;
	if (si114x->led[2].drive_strength)
		si114x->led[2].enable = 1;

	/* Register the sysfs bus entries */
	sysfs_register_bus_entry(si114x, client);

	sensor_class = NULL;
	
#ifdef SI114X_USE_INPUT_POLL
	/* Setup the input subsystem for the ALS infrared light sensor */
	if (setup_als_ir_polled_input(si114x)) {
		dev_err(&client->dev,"%s: Unable to allocate als IR polled input resource\n", __func__);
		goto err_out;
	}
	
	/* Register the sysfs class input entries */
	sysfs_register_class_input_entry_als_ir(si114x, &si114x->input_dev_als_ir.input->dev);

	/* Store the driver data into our private structure */
	si114x->input_dev_als_ir.input_poll->private = si114x;

	/* Setup the input subsystem for the ALS visible light sensor */
	if (setup_als_vis_polled_input(si114x)) {
		dev_err(&client->dev,"%s: Unable to allocate als visible polled input resource\n", __func__);
		goto err_out;
	}

	/* Register the sysfs class input entries */
	sysfs_register_class_input_entry_als_vis(si114x, &si114x->input_dev_als_vis.input->dev);

	/* Store the driver data into our private structure */
	si114x->input_dev_als_vis.input_poll->private = si114x;

	/* Setup the input subsystem for the PS */
	if (setup_ps_polled_input(si114x)) {
		dev_err(&client->dev, "%s: Unable to allocate ps polled input resource\n", __func__);
		goto err_out;
	}
	/* Store the driver data into our private structure */
	sysfs_register_class_input_entry_ps(si114x, &si114x->input_dev_ps.input->dev);
	/* Store the driver data into our private structure */
	si114x->input_dev_ps.input_poll->private = si114x;
#else
	/* Setup the input subsystem for the ALS infrared light. */
	if (setup_als_ir_input(si114x)) {
		dev_err(&client->dev,"%s: Unable to allocate als IR input resource\n", __func__);
		goto err_out;
	}
	/* Register the sysfs class input entries */
	sysfs_register_class_input_entry_als_ir(si114x, &si114x->input_dev_als_ir.input->dev);
	dev_set_drvdata(&si114x->input_dev_als_ir.input->dev, si114x);
	/* Setup the input subsystem for the ALS visible light. */
	if (setup_als_vis_input(si114x)) {
		dev_err(&client->dev,"%s: Unable to allocate als visibile input resource\n", __func__);
		goto err_out;
	}
	/* Register the sysfs class input entries */
	sysfs_register_class_input_entry_als_vis(si114x, &si114x->input_dev_als_ir.input->dev);
	dev_set_drvdata(&si114x->input_dev_als_ir.input->dev, si114x);
	/* Setup the input subsystem for the PS */
	if (setup_ps_input(si114x)) {
		dev_err(&client->dev, "%s: Unable to allocate ps input resource\n", __func__);
		goto err_out;
	}
	dev_set_drvdata(&si114x->input_dev_ps.input->dev, si114x);
#endif  /* SI114X_USE_INPUT_POLL */

	/* Actually setup the device */
	if (si114x_setup(si114x)) {
		goto err_out;
	}

	if (si114x_setup_irq(si114x)) {
		goto err_out;
	}

	/* Setup the device syncronization API via the fileops */
	mutex_init(&si114x_user_lock);
	
	si114x_user_wr_ptr = si114x_fifo_buffer;
	si114x_user_rd_ptr = si114x_fifo_buffer;
	
	misc_register(&si114x_misc);
	
	si114x_private = si114x;
	
	return 0;
	
err_out:
	kfree(si114x);
	
	return -ENODEV;
}

static const struct of_device_id si114x_of_match[] = {
	{ .compatible = "si,si114x" },
	{},
};
MODULE_DEVICE_TABLE(of, si114x_of_match);

static const struct i2c_device_id si114x_id[] = {
	{ DEVICE_NAME, 0 },
};

MODULE_DEVICE_TABLE(i2c, si114x_id);

static struct i2c_driver si114x_driver = {
	.probe = si114x_probe,
	.id_table = si114x_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = DEVICE_NAME,
		.of_match_table = of_match_ptr(si114x_of_match),		
	},
};

static int __init si114x_init(void)
{
	return i2c_add_driver(&si114x_driver);
}
static void __exit si114x_exit(void)
{
	i2c_del_driver(&si114x_driver);
}

module_init(si114x_init);
module_exit(si114x_exit);

MODULE_AUTHOR("cmanton@google.com");
MODULE_DESCRIPTION("SI114x Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);
