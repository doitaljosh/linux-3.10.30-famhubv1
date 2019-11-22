/*
 * Copyright (c) 2010 SAMSUNG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include "sensors_core.h"

/* ltr507 debug */
/*#define DEBUG 1*/
/*#define LTR507_DEBUG*/
/*#define SUPPORT_IRQ_WAKEUP */
/*#define SUPPORT_PROX_IRQ */
#define CHECK_READY_STATUS


#define ltr507_dbgmsg(str, args...) pr_info("%s: " str, __func__, ##args)
#define LTR507_DEBUG
#ifdef LTR507_DEBUG
#define gprintk(fmt, x...) \
	printk(KERN_INFO "%s(%d):" fmt, __func__, __LINE__, ## x)
#else
#define gprintk(x...) do { } while (0)
#endif

#define VENDOR_NAME	"LITEON"
#define CHIP_NAME       	"LTR507"
#define CHIP_ID			0x09


#define LTR507_ALS_CONTR		0x80 /* ALS operation mode, SW reset */
#define LTR507_PS_CONTR		0x81 /* PS operation mode */
#define LTR507_PS_LED			0x82 /* LED pulse modulation frequency, LED current duty cycle and LED peak current. */
#define LTR507_PS_N_PULSES		0x83 /* controls the number of LED pulses to be emitted. */
#define LTR507_PS_MEAS_RATE 	0x84 /* measurement rate*/
#define LTR507_ALS_MEAS_RATE 	0x85 /* ADC Resolution / Bit Width, ALS Measurement Repeat Rate */
#define LTR507_PART_ID 			0x86
#define LTR507_MANUFAC_ID 		0x87
#define LTR507_ALS_DATA 		0x88 /* Direct ALS measurement */
#define LTR507_ALS_DATA_CH1 	0x8d /* 24-bit, little endian */
#define LTR507_ALS_DATA_CH2 	0x90 /* 24-bit, little endian */
#define LTR507_ALS_PS_STATUS 	0x8a
#define LTR507_PS_DATA 			0x8b /* 16-bit, little endian */
#define LTR507_INTR 				0x98 /* output mode, polarity, mode */
#define LTR507_PS_THRESH_UP 	0x99 /* 11 bit, ps upper threshold */
#define LTR507_PS_THRESH_LOW 	0x9b /* 11 bit, ps lower threshold */
#define LTR507_ALS_THRESH_UP 	0x9e /* 16 bit, ALS upper threshold */
#define LTR507_ALS_THRESH_LOW 0xa0 /* 16 bit, ALS lower threshold */
#define LTR507_INTR_PRST 		0xa4 /* ps thresh, als thresh */
#define LTR507_MAX_REG 			0xa5

#define LTR507_ALS_CONTR_SW_RESET 	BIT(2)
#define LTR507_CONTR_PS_GAIN_MASK 	(BIT(3) | BIT(2))
#define LTR507_CONTR_PS_GAIN_SHIFT 	2
#define LTR507_CONTR_ALS_GAIN_MASK 	(BIT(3) | BIT(4))
#define LTR507_CONTR_ACTIVE_MASK		BIT(1)
#define LTR507_CONTR_ACTIVE_SHIFT		1

#define LTR507_ALS_DATA_SHIFT					4
#define LTR507_ALS_DATA_MASK					(0xFFF)

#define LTR507_PS_MEAS_RATE_REPEAT_SHIFT		0
#define LTR507_PS_MEAS_RATE_REPEAT_MASK		(0x07)

#define LTR507_ALS_MEAS_RATE_REPEAT_SHIFT	0
#define LTR507_ALS_MEAS_RATE_REPEAT_MASK	(0x07)
#define LTR507_ALS_MEAS_RATE_ADC_SHIFT		5
#define LTR507_ALS_MEAS_RATE_ADC_MASK		(0xE0)

#define LTR507_INTR_PRST_ALS_PRST_SHIFT		0
#define LTR507_INTR_PRST_ALS_PRST_MASK		(0x0F)
#define LTR507_INTR_PRST_PS_PRST_SHIFT		4
#define LTR507_INTR_PRST_PS_PRST_MASK		(0xF0)

#define LTR507_INTR_INT_MODE_PS_SHIFT		0
#define LTR507_INTR_INT_MODE_PS_MASK			(0x01)
#define LTR507_INTR_INT_MODE_ALS_SHIFT		1
#define LTR507_INTR_INT_MODE_ALS_MASK		(0x02)

#define LTR507_PS_LED_PEAK_CURR_SHIFT		0
#define LTR507_PS_LED_PEAK_CURR_MASK			(0x07)

#define LTR507_STATUS_ALS_INTR	BIT(3)
#define LTR507_STATUS_ALS_RDY 		BIT(2)
#define LTR507_STATUS_PS_INTR 		BIT(1)
#define LTR507_STATUS_PS_RDY 		BIT(0)

#define LTR507_PS_DATA_MASK 		0x7ff
#define LTR507_PS_THRESH_MASK 	0x7ff
#define LTR507_ALS_THRESH_MASK 	0xffff

#define LTR507_LED_PEAK_CURR_5mA		0
#define LTR507_LED_PEAK_CURR_10mA	1
#define LTR507_LED_PEAK_CURR_20mA	2
#define LTR507_LED_PEAK_CURR_50mA	3
#define LTR507_LED_PEAK_CURR_100mA	4

#define LTR507_ALS_DEF_PERIOD 500000
#define LTR507_PS_DEF_PERIOD 100000

#define PS_THRESHOLD_MIN	0
#define PS_THRESHOLD_MAX	0x7FF

#define DEFAULT_PS_THRESHOLD		700
#define DEFAULT_PS_THRESHOLD_HIS	0
#define DEFAULT_PS_SAMPLE_FREQ	10	/* 10Hz = 100ms */
#define DEFAULT_ALS_SAMPLE_FREQ	10	/* 10Hz = 100ms */

enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

enum chan_type {
	CH_TYPE_INTENSITY,
	CH_TYPE_PROXIMITY,
};

enum ps_state {
	PS_STATE_NEAR = 0,
	PS_STATE_FAR = 1,
};

#define OFFSET_ARRAY_LENGTH		10

/* driver data */
struct ltr507_data {
	struct input_dev *proximity_input_dev;
	struct input_dev *light_input_dev;
	struct i2c_client *client;
	struct device *light_dev;
	struct device *proximity_dev;
	int irq;
	struct work_struct work_light;
	struct work_struct work_prox;
	struct mutex lock_als, lock_ps, power_lock;
	u8 als_contr, ps_contr;
	int als_period, ps_period; /* period in micro seconds */

	struct hrtimer timer;
	struct hrtimer prox_timer;
	ktime_t light_poll_delay;
	ktime_t prox_polling_time;
	u8 power_state;

	struct workqueue_struct *wq;
	struct workqueue_struct *wq_prox;

	int threshold;
	int proximity_value;
};

struct ltr507_samp_table {
	int freq_val;  /* repetition frequency in micro HZ*/
	int time_val; /* repetition rate in micro seconds */
};

static const struct ltr507_samp_table ltr507_als_samp_table[] = {
			{10000000, 100000}, {5000000, 200000},
			{2000000, 500000}, {1000000, 1000000},
			{500000, 2000000}, {500000, 2000000},
			{500000, 2000000}, {500000, 2000000},
};

static const struct ltr507_samp_table ltr507_ps_samp_table[] = {
			{80000000, 12500}, {20000000, 50000},
			{14285714, 70000}, {10000000, 100000}, 
			{5000000, 200000}, {2000000, 500000},
			{1000000, 1000000}, {500000, 2000000},
};


static unsigned int ltr507_match_samp_freq(const struct ltr507_samp_table *tab,
					   int len, int val, int val2)
{
	int i, freq;

	freq = val * 1000000 + val2;

	for (i = 0; i < len; i++) {
		if (tab[i].freq_val == freq)
			return i;
	}

	return -EINVAL;
}

static int ltr507_drdy(struct ltr507_data *data, u8 drdy_mask)
{
#ifdef CHECK_READY_STATUS
	int tries = 100;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client,
			LTR507_ALS_PS_STATUS);
		if (ret < 0)
			return ret;
		if ((ret & drdy_mask) == drdy_mask)
			return 0;
		msleep(25);
	}

	dev_err(&data->client->dev, "ltr507_drdy() failed, data not ready\n");
	return -EIO;
#else
	return 0;
#endif
}

static int ltr507_read_als(struct ltr507_data *data, __le32 *buf)
{
	int ret;
	__le32 tmp_als = 0;

	ret = ltr507_drdy(data, LTR507_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;

	/* always read both ALS channels in given order */
	ret = i2c_smbus_read_i2c_block_data(data->client, LTR507_ALS_DATA_CH1, 3, (u8 *)&tmp_als);
	if (ret < 0)
		return ret;

	buf[0] = (tmp_als >> LTR507_ALS_DATA_SHIFT) & LTR507_ALS_DATA_MASK;

	tmp_als = 0;
	ret = i2c_smbus_read_i2c_block_data(data->client, LTR507_ALS_DATA_CH2, 3, (u8 *)&tmp_als);
	if (ret < 0)
		return ret;

	buf[1] = (tmp_als >> LTR507_ALS_DATA_SHIFT) & LTR507_ALS_DATA_MASK;	

	return ret;
}

static int ltr507_read_ps(struct ltr507_data *data)
{
	int ret;

	mutex_lock(&data->lock_ps);

	ret = ltr507_drdy(data, LTR507_STATUS_PS_RDY);
	if (ret < 0)
		goto error_read_ps;
		
	ret = i2c_smbus_read_word_data(data->client, LTR507_PS_DATA);
	if (ret < 0)
		goto error_read_ps;

	ret &= LTR507_PS_DATA_MASK;

error_read_ps:

	mutex_unlock(&data->lock_ps);		
	
	return ret;
}

static int ltr507_als_read_samp_period(struct ltr507_data *data, int *val)
{
	int ret, i;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_MEAS_RATE);	
	if (ret < 0)
		return ret;

	i = ret & LTR507_ALS_MEAS_RATE_REPEAT_MASK;

	if (i < 0 || i >= ARRAY_SIZE(ltr507_als_samp_table))
		return -EINVAL;

	*val = ltr507_als_samp_table[i].time_val;

	return ret;
}

static int ltr507_ps_read_samp_period(struct ltr507_data *data, int *val)
{
	int ret, i;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_MEAS_RATE);	
	if (ret < 0)
		return ret;

	i = ret & LTR507_PS_MEAS_RATE_REPEAT_MASK;

	if (i < 0 || i >= ARRAY_SIZE(ltr507_ps_samp_table))
		return -EINVAL;

	*val = ltr507_ps_samp_table[i].time_val;

	return ret;
}


static int ltr507_read_intr_prst(struct ltr507_data *data,
				 enum chan_type type,
				 int *val2)
{
	int ret, samp_period, prst;

	switch (type) {
		case CH_TYPE_INTENSITY:
			ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
			if (ret < 0)
				return ret;

			prst = ret & LTR507_INTR_PRST_ALS_PRST_MASK;

			ret = ltr507_als_read_samp_period(data, &samp_period);

			if (ret < 0)
				return ret;
				
			*val2 = samp_period * prst;
			return 0;
			
		case CH_TYPE_PROXIMITY:
			ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
			if (ret < 0)
				return ret;

			prst = (ret >> LTR507_INTR_PRST_PS_PRST_SHIFT) & LTR507_INTR_PRST_PS_PRST_MASK;

			ret = ltr507_ps_read_samp_period(data, &samp_period);

			if (ret < 0)
				return ret;

			*val2 = samp_period * prst;
			return 0;
			
		default:
			return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_write_intr_prst(struct ltr507_data *data,
				  enum chan_type type,
				  int val, int val2)
{
	int ret, samp_period, new_val;
	unsigned long period;

	if (val < 0 || val2 < 0)
		return -EINVAL;

	/* period in microseconds */
	period = ((val * 1000000) + val2);

	switch (type) {
	case CH_TYPE_INTENSITY:
		ret = ltr507_als_read_samp_period(data, &samp_period);
		if (ret < 0)
			return ret;

		/* period should be atleast equal to sampling period */
		if (period < samp_period)
			return -EINVAL;

		new_val = DIV_ROUND_UP(period, samp_period);
		if (new_val < 0 || new_val > 0x0f)
			return -EINVAL;

		mutex_lock(&data->lock_als);
		
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
		ret = (ret & (~LTR507_INTR_PRST_ALS_PRST_MASK)) | (new_val << LTR507_INTR_PRST_ALS_PRST_SHIFT);
		ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR_PRST, ret);
		
		mutex_unlock(&data->lock_als);
		if (ret >= 0)
			data->als_period = period;

		return ret;
	case CH_TYPE_PROXIMITY:
		ret = ltr507_ps_read_samp_period(data, &samp_period);
		if (ret < 0)
			return ret;

		/* period should be atleast equal to rate */
		if (period < samp_period)
			return -EINVAL;

		new_val = DIV_ROUND_UP(period, samp_period);
		if (new_val < 0 || new_val > 0x0f)
			return -EINVAL;

		mutex_lock(&data->lock_ps);

		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
		ret = (ret & (~LTR507_INTR_PRST_PS_PRST_MASK)) | (new_val << LTR507_INTR_PRST_PS_PRST_SHIFT);
		ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR_PRST, ret);
		
		mutex_unlock(&data->lock_ps);
		if (ret >= 0)
			data->ps_period = period;

		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_write_intr(struct ltr507_data *data ,
							enum chan_type type,
							int enable)
{
	int ret;

	/* only 1 and 0 are valid inputs */
	if (enable != 1  && enable != 0)
		return -EINVAL;

	switch (type) {
		case CH_TYPE_INTENSITY:
			mutex_lock(&data->lock_als);
			ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);
			ret = (ret & (~LTR507_INTR_INT_MODE_ALS_MASK)) | (enable << LTR507_INTR_INT_MODE_ALS_SHIFT);
			ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR, ret);
			mutex_unlock(&data->lock_als);
			return ret;
		case CH_TYPE_PROXIMITY:
			mutex_lock(&data->lock_ps);
			ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);
			ret = (ret & (~LTR507_INTR_INT_MODE_PS_MASK)) | (enable << LTR507_INTR_INT_MODE_PS_SHIFT);
			ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR, ret);
			mutex_unlock(&data->lock_ps);
			return ret;
		default:
			return -EINVAL;
	}

	return -EINVAL;
}


static int ltr507_als_read_samp_freq(struct ltr507_data *data,
				     int *val, int *val2)
{
	int ret, i;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_MEAS_RATE);
	if (ret < 0)
		return ret;

	i = ret & LTR507_ALS_MEAS_RATE_REPEAT_MASK;

	if (i < 0 || i >= ARRAY_SIZE(ltr507_als_samp_table))
		return -EINVAL;

	*val = ltr507_als_samp_table[i].freq_val / 1000000;
	*val2 = ltr507_als_samp_table[i].freq_val % 1000000;

	return 0;
}

static int ltr507_ps_read_samp_freq(struct ltr507_data *data,
				    int *val, int *val2)
{
	int ret, i;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_MEAS_RATE);
	if (ret < 0)
		return ret;

	i = ret & LTR507_PS_MEAS_RATE_REPEAT_MASK;

	if (i < 0 || i >= ARRAY_SIZE(ltr507_ps_samp_table))
		return -EINVAL;

	*val = ltr507_ps_samp_table[i].freq_val / 1000000;
	*val2 = ltr507_ps_samp_table[i].freq_val % 1000000;

	return 0;
}


static int ltr507_als_write_samp_freq(struct ltr507_data *data,
				      int val, int val2)
{
	int i, ret;

	i = ltr507_match_samp_freq(ltr507_als_samp_table,
				   ARRAY_SIZE(ltr507_als_samp_table),
				   val, val2);

	if (i < 0)
		return i;

	mutex_lock(&data->lock_als);

	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_MEAS_RATE);
	ret = (ret & (~LTR507_ALS_MEAS_RATE_REPEAT_MASK)) | i;
	ret = i2c_smbus_write_byte_data(data->client, LTR507_ALS_MEAS_RATE, ret);

	mutex_unlock(&data->lock_als);

	return ret;
}

static int ltr507_ps_write_samp_freq(struct ltr507_data *data,
				     int val, int val2)
{
	int i, ret;

	i = ltr507_match_samp_freq(ltr507_ps_samp_table,
				   ARRAY_SIZE(ltr507_ps_samp_table),
				   val, val2);

	if (i < 0)
		return i;

	mutex_lock(&data->lock_ps);

	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_MEAS_RATE);
	ret = (ret & (~LTR507_PS_MEAS_RATE_REPEAT_MASK)) | i;
	ret = i2c_smbus_write_byte_data(data->client, LTR507_PS_MEAS_RATE, ret);

	mutex_unlock(&data->lock_ps);

	return ret;
}

static int ltr507_write_sample_freq(struct ltr507_data *data,
				  enum chan_type type,
				    int val, int val2)
{
	int ret, freq_val, freq_val2;

	switch (type) {
		case CH_TYPE_INTENSITY:
			ret = ltr507_als_read_samp_freq(data, &freq_val,
							&freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr507_als_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr507_write_intr_prst(data, type,
						     0, data->als_period);

			if (ret < 0)
				return ltr507_als_write_samp_freq(data,
								  freq_val,
								  freq_val2);
			return ret;
		case CH_TYPE_PROXIMITY:
			ret = ltr507_ps_read_samp_freq(data, &freq_val,
						       &freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr507_ps_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr507_write_intr_prst(data, type,
						     0, data->ps_period);

			if (ret < 0)
				return ltr507_ps_write_samp_freq(data,
								 freq_val,
								 freq_val2);
			return ret;
		default:
			return -EINVAL;
	}
	
	return -EINVAL;
}

static int ltr507_write_contr(struct ltr507_data *data, u8 als_val, u8 ps_val)
{
	int ret;
	int temp_val;
	
	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_CONTR);
	temp_val = (ret & ~LTR507_CONTR_ACTIVE_MASK) | (als_val << LTR507_CONTR_ACTIVE_SHIFT);
	ret = i2c_smbus_write_byte_data(data->client, LTR507_ALS_CONTR, temp_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_CONTR);
	temp_val = (ret & ~LTR507_CONTR_ACTIVE_MASK) | (ps_val << LTR507_CONTR_ACTIVE_SHIFT);
	ret = i2c_smbus_write_byte_data(data->client, LTR507_PS_CONTR, temp_val);
	if (ret < 0)
		return ret;

	return ret;	
}

#if 0
static int ltr507_ps_thresh_write(struct ltr507_data *ltr507, u16 th_high, u16 th_low)
{
	int ret = 0;

	mutex_lock(&ltr507->lock_ps);

	ret = i2c_smbus_write_i2c_block_data(ltr507->client,
				LTR507_PS_THRESH_UP, 2, (u8 *)&th_high);

	ret |= i2c_smbus_write_i2c_block_data(ltr507->client,
				LTR507_PS_THRESH_LOW, 2, (u8 *)&th_low);
				
	mutex_unlock(&ltr507->lock_ps);

	return ret;
}

static int ltr507_ps_thresh_read(struct ltr507_data *ltr507, u16 *pth_high, u16 *pth_low)
{
	int ret = 0;

	mutex_lock(&ltr507->lock_ps);

	ret = i2c_smbus_read_i2c_block_data(ltr507->client,
				LTR507_PS_THRESH_UP, 2, (u8 *)pth_high);

	ret |= i2c_smbus_read_i2c_block_data(ltr507->client,
				LTR507_PS_THRESH_LOW, 2, (u8 *)pth_low);
				
	mutex_unlock(&ltr507->lock_ps);

	return ret;
}
#endif


static __le16 ltr507_read_lux(struct ltr507_data *data)
{
	int ret;
	__le16 lux = 0;
	
	ret = ltr507_drdy(data, LTR507_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;

	mutex_lock(&data->lock_als);

	ret = i2c_smbus_read_i2c_block_data(data->client, LTR507_ALS_DATA, 2, (u8 *)&lux);

	mutex_unlock(&data->lock_als);

	return lux;
}


static void ltr507_light_enable(struct ltr507_data *ltr507)
{
	ltr507_dbgmsg("starting poll timer, delay %lldns\n",
	ktime_to_ns(ltr507->light_poll_delay));
	hrtimer_start(&ltr507->timer, ltr507->light_poll_delay, HRTIMER_MODE_REL);
}

static void ltr507_light_disable(struct ltr507_data *ltr507)
{
	ltr507_dbgmsg("cancelling poll timer\n");
	hrtimer_cancel(&ltr507->timer);
	cancel_work_sync(&ltr507->work_light);
}

static ssize_t poll_delay_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(ltr507->light_poll_delay));
}


static ssize_t poll_delay_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	int64_t new_delay;
	int err;

	err = strict_strtoll(buf, 10, &new_delay);
	if (err < 0)
		return err;

	ltr507_dbgmsg("new delay = %lldns, old delay = %lldns\n",
		    new_delay, ktime_to_ns(ltr507->light_poll_delay));
		    
	mutex_lock(&ltr507->power_lock);
	
	if (new_delay != ktime_to_ns(ltr507->light_poll_delay)) {
		ltr507->light_poll_delay = ns_to_ktime(new_delay);
		if (ltr507->power_state & LIGHT_ENABLED) {
			ltr507_light_disable(ltr507);
			ltr507_light_enable(ltr507);
		}
	}
	
	mutex_unlock(&ltr507->power_lock);

	return size;
}

static ssize_t light_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		       (ltr507->power_state & LIGHT_ENABLED) ? 1 : 0);
}

static ssize_t proximity_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		       (ltr507->power_state & PROXIMITY_ENABLED) ? 1 : 0);
}

static ssize_t light_enable_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	mutex_lock(&ltr507->power_lock);

	ltr507_dbgmsg("new_value = %d, old state = %d\n",
		    new_value, (ltr507->power_state & LIGHT_ENABLED) ? 1 : 0);

	if (new_value && !(ltr507->power_state & LIGHT_ENABLED)) {
		ltr507->als_contr = 1;
		ltr507_write_contr(ltr507, ltr507->als_contr, ltr507->ps_contr);	
		ltr507->power_state |= LIGHT_ENABLED;
		ltr507_light_enable(ltr507);
	} else if (!new_value && (ltr507->power_state & LIGHT_ENABLED)) {
		ltr507_light_disable(ltr507);
		ltr507->power_state &= ~LIGHT_ENABLED;
		ltr507->als_contr = 0;
		ltr507_write_contr(ltr507, ltr507->als_contr, ltr507->ps_contr);	
	}
	
	mutex_unlock(&ltr507->power_lock);
	
	return size;
}

static ssize_t proximity_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	mutex_lock(&ltr507->power_lock);
	
	ltr507_dbgmsg("new_value = %d, old state = %d\n",
		    new_value, (ltr507->power_state & PROXIMITY_ENABLED) ? 1 : 0);

	if (new_value && !(ltr507->power_state & PROXIMITY_ENABLED)) {

		ltr507->power_state |= PROXIMITY_ENABLED;

		input_report_abs(ltr507->proximity_input_dev, ABS_DISTANCE, 1);
		input_sync(ltr507->proximity_input_dev);

		ltr507->ps_contr = 1;

		hrtimer_start(&ltr507->prox_timer, ltr507->prox_polling_time, HRTIMER_MODE_REL);

#ifdef SUPPORT_PROX_IRQ
		enable_irq(ltr507->irq);
#endif
#ifdef SUPPORT_IRQ_WAKEUP
		enable_irq_wake(ltr507->irq);
#endif
	} else if (!new_value && (ltr507->power_state & PROXIMITY_ENABLED)) {

		hrtimer_cancel(&ltr507->prox_timer);
		
#ifdef SUPPORT_IRQ_WAKEUP	
		disable_irq_wake(ltr507->irq);
#endif
#ifdef SUPPORT_PROX_IRQ
		disable_irq(ltr507->irq);
#endif

		ltr507->power_state &= ~PROXIMITY_ENABLED;
		ltr507->ps_contr = 0;
	}

	ltr507_write_intr(ltr507, CH_TYPE_PROXIMITY, ltr507->ps_contr);
	ltr507_write_contr(ltr507, ltr507->als_contr, ltr507->ps_contr);

	ltr507->proximity_value = -1;
	
	mutex_unlock(&ltr507->power_lock);
	
	return size;
}

static ssize_t proximity_thresh_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);

	return sprintf(buf, "prox_threshold = %d\n",  ltr507->threshold);
}

static ssize_t proximity_thresh_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	u16 thresh_value;
	int err = 0;

	err = kstrtou16(buf, 10, &thresh_value);
	if (err < 0)
		pr_err("%s, kstrtou16 failed.", __func__);

	ltr507->threshold = thresh_value;

	pr_info("prox new threshold = (%d)\n", ltr507->threshold);

	return size;
}

static ssize_t get_vendor_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VENDOR_NAME);
}

static ssize_t get_chip_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CHIP_NAME);
}

static DEVICE_ATTR(vendor, S_IRUGO, get_vendor_name, NULL);
static DEVICE_ATTR(name, S_IRUGO, get_chip_name, NULL);

static ssize_t lightsensor_file_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	int adc = 0;
	adc = ltr507_read_lux(ltr507);

	return (ssize_t)sprintf(buf, "%d\n", adc);
}

static DEVICE_ATTR(lux, S_IRUGO, lightsensor_file_state_show, NULL);

static ssize_t lightsensor_raw_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	__le32 als[2];

	ltr507_read_als(ltr507, als);

	return (ssize_t)sprintf(buf, "%d, %d\n", als[0], als[1]);
}

static struct device_attribute dev_attr_light_raw_data =
	__ATTR(raw_data, S_IRUGO, lightsensor_raw_data_show, NULL);
static struct device_attribute dev_attr_light_enable =
	__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	       light_enable_show, light_enable_store);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		   poll_delay_show, poll_delay_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static struct device_attribute *light_sensor_attrs[] = {
	&dev_attr_lux,
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_light_raw_data,
	NULL
};


static ssize_t proximity_state_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct ltr507_data *ltr507 = dev_get_drvdata(dev);
	int ret;

	ret = ltr507_read_ps(ltr507);
	if (ret < 0)
		return ret;
		
	return (ssize_t)sprintf(buf, "%d\n", ret);
}

static struct device_attribute dev_attr_proximity_enable =
	__ATTR(enable, S_IRUGO | S_IWUSR,
	       proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_proximity_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

static struct device_attribute dev_attr_proximity_raw_data =
	__ATTR(raw_data, S_IRUGO, proximity_state_show, NULL);

static DEVICE_ATTR(prox_thresh, S_IRUGO | S_IWUSR, proximity_thresh_show,
	proximity_thresh_store);

static struct device_attribute *prox_sensor_attrs[] = {
	&dev_attr_proximity_raw_data,
	&dev_attr_prox_thresh,
	&dev_attr_vendor,
	&dev_attr_name,
	NULL
};

static void ltr507_work_func_light(struct work_struct *work)
{
	struct ltr507_data *ltr507 = container_of(work, struct ltr507_data,
					      work_light);
	int adc = ltr507_read_lux(ltr507);

	input_report_rel(ltr507->light_input_dev, REL_MISC, adc);
	input_sync(ltr507->light_input_dev);
}

static void ltr507_work_func_prox(struct work_struct *work)
{
	struct ltr507_data *ltr507 =
		container_of(work, struct ltr507_data, work_prox);
	int proximity_value = 0;
	int raw_ps;

	/* change Threshold */
	raw_ps = ltr507_read_ps(ltr507);

	if (raw_ps >=  ltr507->threshold)
		proximity_value = PS_STATE_NEAR;
	else
		proximity_value = PS_STATE_FAR;

	if (ltr507->proximity_value != proximity_value)
	{
		input_report_abs(ltr507->proximity_input_dev,
						ABS_DISTANCE, proximity_value);
		input_sync(ltr507->proximity_input_dev);

		pr_info("prox value = (%s)\n", (proximity_value==PS_STATE_NEAR)?"NEAR":"FAR");
	}
	
	ltr507->proximity_value = proximity_value;

#ifdef SUPPORT_PROX_IRQ
	/* enable INT */
	enable_irq(ltr507->irq);
#endif
}



/* This function is for light sensor.  It operates every a few seconds.
 * It asks for work to be done on a thread because i2c needs a thread
 * context (slow and blocking) and then reschedules the timer to run again.
 */
static enum hrtimer_restart ltr507_timer_func(struct hrtimer *timer)
{
	struct ltr507_data *ltr507 = container_of(timer, struct ltr507_data, timer);
	queue_work(ltr507->wq, &ltr507->work_light);
	hrtimer_forward_now(&ltr507->timer, ltr507->light_poll_delay);
	return HRTIMER_RESTART;
}

static enum hrtimer_restart ltr507_prox_timer_func(struct hrtimer *timer)
{
	struct ltr507_data *ltr507 = container_of(timer, struct ltr507_data,
		prox_timer);
	queue_work(ltr507->wq_prox, &ltr507->work_prox);
	hrtimer_forward_now(&ltr507->prox_timer, ltr507->prox_polling_time);

	return HRTIMER_RESTART;
}

#ifdef SUPPORT_PROX_IRQ
/* interrupt happened due to transition/change of near/far proximity state */
static irqreturn_t ltr507_interrupt_handler(int irq, void *private)
{
	struct ltr507_data *ip = private;

	disable_irq_nosync(ip->irq);
	queue_work(ip->wq, &ip->work_prox);

	return IRQ_HANDLED;
}
#endif

static int ltr507_device_init(struct ltr507_data *data)
{
	int ret;

	ret = ltr507_read_intr_prst(data, CH_TYPE_INTENSITY, &data->als_period);
	if (ret < 0)
		return ret;

	ret = ltr507_read_intr_prst(data, CH_TYPE_PROXIMITY, &data->ps_period);
	if (ret < 0)
		return ret;

	ret = ltr507_write_sample_freq(data, CH_TYPE_INTENSITY, 
								DEFAULT_ALS_SAMPLE_FREQ, 0);		/* 2Hz = 500ms */
	if (ret < 0)
		return ret;

	ret = ltr507_write_sample_freq(data, CH_TYPE_PROXIMITY, 
								DEFAULT_PS_SAMPLE_FREQ, 0);		/* 10Hz = 100ms */
	if (ret < 0)
		return ret;

	/*
		set default led peak current
	*/
	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_LED);
	if (ret < 0)
		return ret;

	ret  = (ret & LTR507_PS_LED_PEAK_CURR_MASK) | LTR507_LED_PEAK_CURR_100mA;

	ret = i2c_smbus_write_byte_data(data->client, LTR507_PS_LED, ret);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, LTR507_PS_N_PULSES, 255);		/* Number of pulses = 255 */
	if (ret < 0)
		return ret;

	data->als_contr = 0;
	data->ps_contr = 0;

	ret = ltr507_write_contr(data, data->als_contr, data->ps_contr);	
		
	return ret;
}

static int ltr507_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	u8 partid = 0;
	struct input_dev *input_dev;
	struct ltr507_data *ltr507;
//	u32 interrupt_num = 0;

	pr_info("%s, is called\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		goto exit;
	}

	ltr507 = kzalloc(sizeof(struct ltr507_data), GFP_KERNEL);
	if (!ltr507) {
		pr_err("%s: failed to alloc memory for module data\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}
	
	ltr507->proximity_value = -1;
	ltr507->threshold = DEFAULT_PS_THRESHOLD;
	
	ltr507->client = client;
	i2c_set_clientdata(client, ltr507);

	partid = i2c_smbus_read_byte_data(ltr507->client, LTR507_PART_ID);
	if (partid < 0) {
		dev_err(&client->dev, "%s: i2c read error [%x]\n", __func__, partid);
		goto err_chip_id_or_i2c_error;
	}

	mutex_init(&ltr507->power_lock);
	mutex_init(&ltr507->lock_als);
	mutex_init(&ltr507->lock_ps);

	/* allocate proximity input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_allocate_device_proximity;
	}

	ltr507->proximity_input_dev = input_dev;
	input_set_drvdata(input_dev, ltr507);
	input_dev->name = "proximity_sensor";
	input_set_capability(input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	input_report_abs(ltr507->proximity_input_dev, ABS_DISTANCE, 1);
	input_sync(ltr507->proximity_input_dev);

	ltr507_dbgmsg("registering proximity input device\n");
	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err("%s: could not register proximity input device\n", __func__);
		goto err_input_register_device_proximity;
	}

	ret = sensors_create_symlink(&ltr507->proximity_input_dev->dev.kobj,
				input_dev->name);
	if (ret < 0) {
		pr_err("%s: could not create proximity symlink\n", __func__);
		goto err_create_symlink_proximity;
	}

	ret = sysfs_create_group(&input_dev->dev.kobj,
				 &proximity_attribute_group);
	if (ret) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group_proximity;
	}

	/* hrtimer settings.  we poll for light values using a timer. */
	hrtimer_init(&ltr507->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ltr507->light_poll_delay = ns_to_ktime(500 * NSEC_PER_MSEC);
	ltr507->timer.function = ltr507_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	ltr507->wq = create_singlethread_workqueue("ltr507_wq");
	if (!ltr507->wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto err_create_workqueue;
	}

	ltr507->wq_prox = create_singlethread_workqueue("ltr507_wq_prox");
	if (!ltr507->wq_prox) {
		ret = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto err_create_workqueue;
	}

	/* this is the thread function we run on the work queue */
	INIT_WORK(&ltr507->work_light, ltr507_work_func_light);
	INIT_WORK(&ltr507->work_prox, ltr507_work_func_prox);

	hrtimer_init(&ltr507->prox_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ltr507->prox_polling_time = ns_to_ktime(300 * NSEC_PER_MSEC);
	ltr507->prox_timer.function = ltr507_prox_timer_func;

	/* allocate lightsensor-level input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		ret = -ENOMEM;
		goto err_input_allocate_device_light;
	}

	input_set_drvdata(input_dev, ltr507);
	input_dev->name = "light_sensor";
	input_set_capability(input_dev, EV_REL, REL_MISC);

	ltr507_dbgmsg("registering lightsensor-level input device\n");
	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err("%s: could not register light input device\n", __func__);
		goto err_input_register_device_light;
	}

	ltr507->light_input_dev = input_dev;
	ret = sensors_create_symlink(&ltr507->light_input_dev->dev.kobj,
				input_dev->name);
	if (ret < 0) {
		pr_err("%s: could not create light symlink\n", __func__);
		goto err_create_symlink_light;
	}

	ret = sysfs_create_group(&input_dev->dev.kobj,
				 &light_attribute_group);
	if (ret) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group_light;
	}

	ret = ltr507_device_init(ltr507);
	if (ret < 0) {
		dev_err(&client->dev, "ltr507_device_init error %d\n", ret);
		goto err_sysfs_create_group_light;
	}

#ifdef SUPPORT_PROX_IRQ
#if defined(CONFIG_ARCH_SDP)
	if (of_property_read_u32(client->dev.of_node, "ext-int", &interrupt_num)) {
		dev_err(&client->dev, "could not get interrupt number\n");
		goto err_sysfs_create_group_light;
	} else {
		client->irq = 160 + interrupt_num + 32; 
		dev_dbg(&client->dev, "%s: interrupt number is EXTINT%d\n", __func__, interrupt_num);
	}
#endif

	dev_info(&client->dev, "interrupt (%d)\n", client->irq);

	ret = request_irq(client->irq,
					ltr507_interrupt_handler,
					IRQF_DISABLED | IRQF_TRIGGER_RISING,
					"ltr507_thresh_event",
					ltr507);
				
	if (ret) {
		dev_err(&client->dev, "request irq (%d) failed\n",
			client->irq);
		goto err_setup_irq;
	}

	/* start with interrupts disabled */
	disable_irq(client->irq);

	ltr507->irq = client->irq;
#endif

	/* set sysfs for proximity sensor and light sensor */
	ret = sensors_register(ltr507->proximity_dev,
				ltr507, prox_sensor_attrs, "proximity_sensor");
	if (ret) {
		pr_err("%s: cound not register proximity sensor device(%d).\n",
			__func__, ret);
		goto err_proximity_sensor_register_failed;
	}

	ret = sensors_register(ltr507->light_dev,
				ltr507, light_sensor_attrs, "light_sensor");
	if (ret) {
		pr_err("%s: cound not register light sensor device(%d).\n",
			__func__, ret);
		goto err_light_sensor_register_failed;
	}

	pr_info("%s is done.", __func__);
	return ret;

	/* error, unwind it all */
err_light_sensor_register_failed:
	sensors_unregister(ltr507->proximity_dev, prox_sensor_attrs);

err_proximity_sensor_register_failed:
#ifdef SUPPORT_PROX_IRQ
	free_irq(ltr507->irq, 0);
err_setup_irq:
#endif
	sysfs_remove_group(&ltr507->light_input_dev->dev.kobj,
			   &light_attribute_group);
err_sysfs_create_group_light:
err_create_symlink_light:
	sensors_remove_symlink(&ltr507->light_input_dev->dev.kobj,
				ltr507->light_input_dev->name);
	input_unregister_device(ltr507->light_input_dev);
err_input_register_device_light:
	input_free_device(input_dev);
err_input_allocate_device_light:
	destroy_workqueue(ltr507->wq);
err_create_workqueue:
	sysfs_remove_group(&ltr507->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
err_sysfs_create_group_proximity:
err_create_symlink_proximity:
	sensors_remove_symlink(&ltr507->proximity_input_dev->dev.kobj,
				ltr507->proximity_input_dev->name);
	input_unregister_device(ltr507->proximity_input_dev);
err_input_register_device_proximity:
	input_free_device(input_dev);
err_input_allocate_device_proximity:
	mutex_destroy(&ltr507->power_lock);
	mutex_destroy(&ltr507->lock_ps);
	mutex_destroy(&ltr507->lock_als);
err_chip_id_or_i2c_error:
	kfree(ltr507);
exit:
	pr_err("%s failed. ret = %d\n", __func__, ret);
	return ret;
}

static int ltr507_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr507_data *ltr507 = i2c_get_clientdata(client);

	return ltr507_write_contr(ltr507, 0, 0);
}

static int ltr507_resume(struct device *dev)
{
	/* Turn power back on if we were before suspend. */
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr507_data *ltr507 = i2c_get_clientdata(client);

	return ltr507_write_contr(ltr507, ltr507->als_contr,
			ltr507->ps_contr);
}

static int ltr507_i2c_remove(struct i2c_client *client)
{
	struct ltr507_data *ltr507 = i2c_get_clientdata(client);

	sensors_unregister(ltr507->proximity_dev, prox_sensor_attrs);

	sensors_remove_symlink(&ltr507->light_input_dev->dev.kobj,
					ltr507->light_input_dev->name);
	sensors_remove_symlink(&ltr507->proximity_input_dev->dev.kobj,
					ltr507->proximity_input_dev->name);

	sysfs_remove_group(&ltr507->light_input_dev->dev.kobj,
			   &light_attribute_group);
	input_unregister_device(ltr507->light_input_dev);

	sysfs_remove_group(&ltr507->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
	input_unregister_device(ltr507->proximity_input_dev);

	//free_irq(ltr507->irq, NULL);

	ltr507_write_contr(ltr507, 0, 0);

	hrtimer_cancel(&ltr507->prox_timer);

	destroy_workqueue(ltr507->wq);
	destroy_workqueue(ltr507->wq_prox);

	mutex_destroy(&ltr507->power_lock);
	mutex_destroy(&ltr507->lock_ps);
	mutex_destroy(&ltr507->lock_als);
	
	kfree(ltr507);

	return 0;
}

static const struct of_device_id ltr507_of_match[] = {
	{ .compatible = "liteon,ltr507" },
	{},
};
MODULE_DEVICE_TABLE(of, ltr507_of_match);

static const struct i2c_device_id ltr507_device_id[] = {
	{"ltr507", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ltr507_device_id);

static const struct dev_pm_ops ltr507_pm_ops = {
	.suspend = ltr507_suspend,
	.resume = ltr507_resume
};

static struct i2c_driver ltr507_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ltr507",
		.pm = &ltr507_pm_ops,
		.of_match_table = of_match_ptr(ltr507_of_match),
	},
	.probe	= ltr507_i2c_probe,
	.remove	= ltr507_i2c_remove,
	.id_table	= ltr507_device_id,
};

static int __init ltr507_init(void)
{
	return i2c_add_driver(&ltr507_i2c_driver);
}

static void __exit ltr507_exit(void)
{
	i2c_del_driver(&ltr507_i2c_driver);
}

module_init(ltr507_init);
module_exit(ltr507_exit);

MODULE_AUTHOR("SAMSUNG");
MODULE_DESCRIPTION("Lite-On LTR507 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");

