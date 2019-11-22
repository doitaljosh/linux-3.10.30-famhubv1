/*
 * ltr507.c - Support for Lite-On LTR507 ambient light and proximity sensor
 *
 * Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * 7-bit I2C slave address 0x23
 *
 * TODO: IR LED characteristics
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define LTR507_DRV_NAME "ltr507"

#define LTR507_ALS_CONTR		0x80 /* ALS operation mode, SW reset */
#define LTR507_PS_CONTR		0x81 /* PS operation mode */
#define LTR507_PS_LED			0x82 /* LED pulse modulation frequency, LED current duty cycle and LED peak current. */
#define LTR507_PS_N_PULSES		0x83 /* controls the number of LED pulses to be emitted. */
#define LTR507_PS_MEAS_RATE 	0x84 /* measurement rate*/
#define LTR507_ALS_MEAS_RATE 	0x85 /* ADC Resolution / Bit Width, ALS Measurement Repeat Rate */
#define LTR507_PART_ID 			0x86
#define LTR507_MANUFAC_ID 		0x87
#define LTR507_ALS_DATA0 		0x8d /* 24-bit, little endian */
#define LTR507_ALS_DATA1 		0x90 /* 24-bit, little endian */
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
#define LTR507_CONTR_ACTIVE 			BIT(1)

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
#define LTR507_LUX_CONV(vis_coeff, vis_data, ir_coeff, ir_data) \
			((vis_coeff * vis_data) - (ir_coeff * ir_data))

struct ltr507_samp_table {
	int freq_val;  /* repetition frequency in micro HZ*/
	int time_val; /* repetition rate in micro seconds */
};

#define LTR507_RESERVED_GAIN -1

enum {
	ltr507 = 0,
	ltr559,
	ltr301,
};

struct ltr507_gain {
	int scale;
	int uscale;
};

static struct ltr507_gain ltr507_als_gain_tbl[] = {
	{1, 0},
	{0, 500000},
	{0, 10000},
	{0, 5000},
};

static struct ltr507_gain ltr559_als_gain_tbl[] = {
	{1, 0},
	{0, 500000},
	{0, 250000},
	{0, 125000},
	{LTR507_RESERVED_GAIN, LTR507_RESERVED_GAIN},
	{LTR507_RESERVED_GAIN, LTR507_RESERVED_GAIN},
	{0, 20000},
	{0, 10000},
};

static struct ltr507_gain ltr507_ps_gain_tbl[] = {
	{1, 0},
	{0, 250000},
	{0, 125000},
	{0, 62500},
};

static struct ltr507_gain ltr559_ps_gain_tbl[] = {
	{0, 62500}, /* x16 gain */
	{0, 31250}, /* x32 gain */
	{0, 15625}, /* bits X1 are for x64 gain */
	{0, 15624},
};

struct ltr507_chip_info {
	u8 partid;
	struct ltr507_gain *als_gain;
	int als_gain_tbl_size;
	struct ltr507_gain *ps_gain;
	int ps_gain_tbl_size;
	u8 als_mode_active;
	u8 als_gain_mask;
	u8 als_gain_shift;
	struct iio_chan_spec const *channels;
	const int no_channels;
	const struct iio_info *info;
	const struct iio_info *info_no_irq;
};

struct ltr507_data {
	struct i2c_client *client;
	struct mutex lock_als, lock_ps;
	struct ltr507_chip_info *chip_info;
	u8 als_contr, ps_contr;
	int als_period, ps_period; /* period in micro seconds */
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

	return IIO_VAL_INT_PLUS_MICRO;
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

	return IIO_VAL_INT_PLUS_MICRO;
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

	return IIO_VAL_INT;
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

	return IIO_VAL_INT;
}

/* IR and visible spectrum coeff's are given in data sheet */
static unsigned long ltr507_calculate_lux(u32 vis_data, u32 ir_data)
{
	unsigned long ratio, lux;

	if (vis_data == 0)
		return 0;

	/* multiply numerator by 100 to avoid handling ratio < 1 */
	ratio = DIV_ROUND_UP(ir_data * 100, ir_data + vis_data);

	if (ratio < 45)
		lux = LTR507_LUX_CONV(1774, vis_data, -1105, ir_data);
	else if (ratio >= 45 && ratio < 64)
		lux = LTR507_LUX_CONV(3772, vis_data, 1336, ir_data);
	else if (ratio >= 64 && ratio < 85)
		lux = LTR507_LUX_CONV(1690, vis_data, 169, ir_data);
	else
		lux = 0;

	return lux / 1000;
}

static int ltr507_drdy(struct ltr507_data *data, u8 drdy_mask)
{
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
}

static int ltr507_read_als(struct ltr507_data *data, __le32 *buf)
{
	int ret;
	__le32 tmp_als = 0;

	ret = ltr507_drdy(data, LTR507_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;

	/* always read both ALS channels in given order */
	ret = i2c_smbus_read_i2c_block_data(data->client, LTR507_ALS_DATA0, 3, (u8 *)&tmp_als);
	if (ret < 0)
		return ret;

	buf[0] = (tmp_als >> LTR507_ALS_DATA_SHIFT) & LTR507_ALS_DATA_MASK;

	tmp_als = 0;
	ret = i2c_smbus_read_i2c_block_data(data->client, LTR507_ALS_DATA1, 3, (u8 *)&tmp_als);
	if (ret < 0)
		return ret;

	buf[1] = (tmp_als >> LTR507_ALS_DATA_SHIFT) & LTR507_ALS_DATA_MASK;	

	printk("als0 = %d\n", buf[0]);
	printk("als1 = %d\n", buf[1]);

	return ret;	
}

static int ltr507_read_ps(struct ltr507_data *data)
{
	int ret;

	ret = ltr507_drdy(data, LTR507_STATUS_PS_RDY);
	if (ret < 0)
		return ret;
	return i2c_smbus_read_word_data(data->client, LTR507_PS_DATA);
}

static int ltr507_read_intr_prst(struct ltr507_data *data,
				 enum iio_chan_type type,
				 int *val2)
{
	int ret, samp_period, prst;

	switch (type) {
	case IIO_INTENSITY:
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
		if (ret < 0)
			return ret;

		prst = ret & LTR507_INTR_PRST_ALS_PRST_MASK;

		ret = ltr507_als_read_samp_period(data, &samp_period);

		if (ret < 0)
			return ret;
			
		*val2 = samp_period * prst;
		return IIO_VAL_INT_PLUS_MICRO;
		
	case IIO_PROXIMITY:
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR_PRST);
		if (ret < 0)
			return ret;

		prst = (ret >> LTR507_INTR_PRST_PS_PRST_SHIFT) & LTR507_INTR_PRST_PS_PRST_MASK;

		ret = ltr507_ps_read_samp_period(data, &samp_period);

		if (ret < 0)
			return ret;

		*val2 = samp_period * prst;
		return IIO_VAL_INT_PLUS_MICRO;
		
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_write_intr_prst(struct ltr507_data *data,
				  enum iio_chan_type type,
				  int val, int val2)
{
	int ret, samp_period, new_val;
	unsigned long period;

	if (val < 0 || val2 < 0)
		return -EINVAL;

	/* period in microseconds */
	period = ((val * 1000000) + val2);

	switch (type) {
	case IIO_INTENSITY:
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
	case IIO_PROXIMITY:
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

#define LTR507_INTENSITY_CHANNEL(_idx, _addr, _mod, _shared) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.address = (_addr), \
	.channel2 = (_mod), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = (_shared), \
	.scan_index = (_idx), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 20, \
		.storagebits = 32, \
		.shift = 5, \
		.endianness = IIO_CPU, \
	}\
}

#define LTR507_LIGHT_CHANNEL() { \
	.type = IIO_LIGHT, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), \
	.scan_index = -1, \
}

static const struct iio_chan_spec ltr507_channels[] = {
	LTR507_LIGHT_CHANNEL(),
	LTR507_INTENSITY_CHANNEL(0, LTR507_ALS_DATA0, IIO_MOD_LIGHT_BOTH, 0),
	LTR507_INTENSITY_CHANNEL(1, LTR507_ALS_DATA1, IIO_MOD_LIGHT_IR,
				 BIT(IIO_CHAN_INFO_SCALE) |
				 BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	{
		.type = IIO_PROXIMITY,
		.address = LTR507_PS_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 11,
			.storagebits = 16,
			.endianness = IIO_CPU,
		}\
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec ltr301_channels[] = {
	LTR507_LIGHT_CHANNEL(),
	LTR507_INTENSITY_CHANNEL(0, LTR507_ALS_DATA0, IIO_MOD_LIGHT_BOTH, 0),
	LTR507_INTENSITY_CHANNEL(1, LTR507_ALS_DATA1, IIO_MOD_LIGHT_IR,
				 BIT(IIO_CHAN_INFO_SCALE) |
				 BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static int ltr507_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	__le32 buf[2];
	int ret, i;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		switch (chan->type) {
		case IIO_LIGHT:
			mutex_lock(&data->lock_als);
			ret = ltr507_read_als(data, buf);
			mutex_unlock(&data->lock_als);
			if (ret < 0)
				return ret;
			*val = ltr507_calculate_lux(le32_to_cpu(buf[1]),
						    le32_to_cpu(buf[0]));
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		switch (chan->type) {
		case IIO_INTENSITY:
			mutex_lock(&data->lock_als);
			ret = ltr507_read_als(data, buf);
			mutex_unlock(&data->lock_als);
			if (ret < 0)
				return ret;
			*val = le32_to_cpu(chan->address == LTR507_ALS_DATA0 ?
				buf[0] : buf[1]);
			return IIO_VAL_INT;
		case IIO_PROXIMITY:
			mutex_lock(&data->lock_ps);
			ret = ltr507_read_ps(data);
			mutex_unlock(&data->lock_ps);
			if (ret < 0)
				return ret;
			*val = ret & LTR507_PS_DATA_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			i = (data->als_contr & data->chip_info->als_gain_mask)
			     >> data->chip_info->als_gain_shift;
			*val = data->chip_info->als_gain[i].scale;
			*val2 = data->chip_info->als_gain[i].uscale;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_PROXIMITY:
			i = (data->ps_contr & LTR507_CONTR_PS_GAIN_MASK) >>
				LTR507_CONTR_PS_GAIN_SHIFT;
			*val = data->chip_info->ps_gain[i].scale;
			*val2 = data->chip_info->ps_gain[i].uscale;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_INTENSITY:
			return ltr507_als_read_samp_freq(data, val, val2);
		case IIO_PROXIMITY:
			return ltr507_ps_read_samp_freq(data, val, val2);
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static int ltr507_get_gain_index(struct ltr507_gain *gain, int size,
				 int val, int val2)
{
	int i;

	for (i = 0; i < size; i++)
		if (val == gain[i].scale && val2 == gain[i].uscale)
			return i;

	return -1;
}

static int ltr507_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int i, ret, freq_val, freq_val2;
	struct ltr507_chip_info *info = data->chip_info;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			i = ltr507_get_gain_index(info->als_gain,
						  info->als_gain_tbl_size,
						  val, val2);
			if (i < 0)
				return -EINVAL;

			data->als_contr &= ~info->als_gain_mask;
			data->als_contr |= i << info->als_gain_shift;
			
			return i2c_smbus_write_byte_data(data->client, LTR507_ALS_CONTR, 
						data->als_contr);
		case IIO_PROXIMITY:
			i = ltr507_get_gain_index(info->ps_gain,
						  info->ps_gain_tbl_size,
						  val, val2);
			if (i < 0)
				return -EINVAL;
			data->ps_contr &= ~LTR507_CONTR_PS_GAIN_MASK;
			data->ps_contr |= i << LTR507_CONTR_PS_GAIN_SHIFT;

			return i2c_smbus_write_byte_data(data->client, LTR507_PS_CONTR,
						data->ps_contr);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_INTENSITY:
			ret = ltr507_als_read_samp_freq(data, &freq_val,
							&freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr507_als_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr507_write_intr_prst(data, chan->type,
						     0, data->als_period);

			if (ret < 0)
				return ltr507_als_write_samp_freq(data,
								  freq_val,
								  freq_val2);
			return ret;
		case IIO_PROXIMITY:
			ret = ltr507_ps_read_samp_freq(data, &freq_val,
						       &freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr507_ps_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr507_write_intr_prst(data, chan->type,
						     0, data->ps_period);

			if (ret < 0)
				return ltr507_ps_write_samp_freq(data,
								 freq_val,
								 freq_val2);
			return ret;
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static int ltr507_read_thresh(struct iio_dev *indio_dev,
			       u64 event_code,
			       int *val)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret, thresh_data;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_INTENSITY:
		switch (IIO_EVENT_CODE_EXTRACT_DIR(event_code)) {
		case IIO_EV_DIR_RISING:
			ret = i2c_smbus_read_i2c_block_data(data->client,
						LTR507_ALS_THRESH_UP, 2, (u8 *)&thresh_data);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR507_ALS_THRESH_MASK;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = i2c_smbus_read_i2c_block_data(data->client,
						LTR507_ALS_THRESH_LOW, 2, (u8 *)&thresh_data);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR507_ALS_THRESH_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_PROXIMITY:
		switch (IIO_EVENT_CODE_EXTRACT_DIR(event_code)) {
		case IIO_EV_DIR_RISING:
			ret = i2c_smbus_read_i2c_block_data(data->client,
						LTR507_PS_THRESH_UP, 2, (u8 *)&thresh_data);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR507_PS_THRESH_MASK;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = i2c_smbus_read_i2c_block_data(data->client,
						LTR507_PS_THRESH_LOW, 2, (u8 *)&thresh_data);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR507_PS_THRESH_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_write_thresh(struct iio_dev *indio_dev,
			       u64 event_code,
			       int val)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret;

	if (val < 0)
		return -EINVAL;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_INTENSITY:
		if (val > LTR507_ALS_THRESH_MASK)
			return -EINVAL;
		switch (IIO_EVENT_CODE_EXTRACT_DIR(event_code)) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&data->lock_als);
			ret = i2c_smbus_write_i2c_block_data(data->client,
						LTR507_ALS_THRESH_UP, 2, (u8 *)&val);
			mutex_unlock(&data->lock_als);
			return ret;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&data->lock_als);
			ret = i2c_smbus_write_i2c_block_data(data->client,
						LTR507_ALS_THRESH_LOW, 2, (u8 *)&val);
			mutex_unlock(&data->lock_als);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_PROXIMITY:
		if (val > LTR507_PS_THRESH_MASK)
			return -EINVAL;
		switch (IIO_EVENT_CODE_EXTRACT_DIR(event_code)) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&data->lock_ps);
			ret = i2c_smbus_write_i2c_block_data(data->client,
						LTR507_PS_THRESH_UP, 2, (u8 *)&val);
			mutex_unlock(&data->lock_ps);
			return ret;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&data->lock_ps);
			ret = i2c_smbus_write_i2c_block_data(data->client,
						LTR507_PS_THRESH_LOW, 2, (u8 *)&val);
			mutex_unlock(&data->lock_ps);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_read_event(struct iio_dev *indio_dev,
			       u64 event_code,
			       int *val)
{
	return ltr507_read_thresh(indio_dev, event_code, val);
}

static int ltr507_write_event(struct iio_dev *indio_dev,
			       u64 event_code,
			       int val)
{
	return ltr507_write_thresh(indio_dev, event_code, val);
}

static int ltr507_read_event_config(struct iio_dev *indio_dev,
				 					u64 event_code)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret, status;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_INTENSITY:
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);		
		if (ret < 0)
			return ret;
		status = (ret & LTR507_INTR_INT_MODE_ALS_MASK) >> LTR507_INTR_INT_MODE_ALS_SHIFT;
		return status;
	case IIO_PROXIMITY:
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);
		if (ret < 0)
			return ret;
		status = (ret & LTR507_INTR_INT_MODE_PS_MASK) >> LTR507_INTR_INT_MODE_PS_SHIFT;
		return status;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr507_write_event_config(struct iio_dev *indio_dev,
									  u64 event_code,
									  int state)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret;

	/* only 1 and 0 are valid inputs */
	if (state != 1  && state != 0)
		return -EINVAL;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_INTENSITY:
		mutex_lock(&data->lock_als);
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);
		ret = (ret & (~LTR507_INTR_INT_MODE_ALS_MASK)) | (state << LTR507_INTR_INT_MODE_ALS_SHIFT);
		ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR, ret);
		mutex_unlock(&data->lock_als);
		return ret;
	case IIO_PROXIMITY:
		mutex_lock(&data->lock_ps);
		ret = i2c_smbus_read_byte_data(data->client, LTR507_INTR);
		ret = (ret & (~LTR507_INTR_INT_MODE_PS_MASK)) | (state << LTR507_INTR_INT_MODE_PS_SHIFT);
		ret = i2c_smbus_write_byte_data(data->client, LTR507_INTR, ret);
		mutex_unlock(&data->lock_ps);
		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static ssize_t ltr507_show_proximity_scale_avail(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ltr507_data *data = iio_priv(dev_to_iio_dev(dev));
	struct ltr507_chip_info *info = data->chip_info;
	ssize_t len = 0;
	int i;

	for (i = 0; i < info->ps_gain_tbl_size; i++) {
		if (info->ps_gain[i].scale == LTR507_RESERVED_GAIN)
			continue;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 info->ps_gain[i].scale,
				 info->ps_gain[i].uscale);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t ltr507_show_intensity_scale_avail(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ltr507_data *data = iio_priv(dev_to_iio_dev(dev));
	struct ltr507_chip_info *info = data->chip_info;
	ssize_t len = 0;
	int i;

	for (i = 0; i < info->als_gain_tbl_size; i++) {
		if (info->als_gain[i].scale == LTR507_RESERVED_GAIN)
			continue;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 info->als_gain[i].scale,
				 info->als_gain[i].uscale);
	}

	buf[len - 1] = '\n';

	return len;
}

#if defined(CONFIG_DEBUG_FS)
static int ltr507_reg_access(struct iio_dev *indio_dev,
			      unsigned reg, unsigned writeval,
			      unsigned *readval)
{
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret;

	if (readval) {
		ret = i2c_smbus_read_byte_data(data->client, reg);
		if (ret < 0)
			return ret;
		*readval = ret;
		ret = 0;
	} else
		ret = i2c_smbus_write_byte_data(data->client, reg, writeval);

	return ret;
}
#endif

static IIO_CONST_ATTR_INT_TIME_AVAIL("1.2 0.6 0.3 0.15 0.075 0.004685 0.000292 0.000018");
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 5 2 1 0.5");


static IIO_DEVICE_ATTR(in_proximity_scale_available, S_IRUGO,
		       ltr507_show_proximity_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_intensity_scale_available, S_IRUGO,
		       ltr507_show_intensity_scale_avail, NULL, 0);

static struct attribute *ltr507_attributes[] = {
	&iio_dev_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_dev_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static struct attribute *ltr301_attributes[] = {
	&iio_dev_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ltr507_attribute_group = {
	.attrs = ltr507_attributes,
};

static const struct attribute_group ltr301_attribute_group = {
	.attrs = ltr301_attributes,
};

static const struct iio_info ltr507_info_no_irq = {
	.read_raw = ltr507_read_raw,
	.write_raw = ltr507_write_raw,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_reg_access = ltr507_reg_access,
#endif
	.attrs = &ltr507_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ltr507_info = {
	.read_raw = ltr507_read_raw,
	.write_raw = ltr507_write_raw,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_reg_access = ltr507_reg_access,
#endif
	.attrs = &ltr507_attribute_group,
	.read_event_value	= &ltr507_read_event,
	.write_event_value	= &ltr507_write_event,
	.read_event_config	= &ltr507_read_event_config,
	.write_event_config	= &ltr507_write_event_config,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ltr301_info_no_irq = {
	.read_raw = ltr507_read_raw,
	.write_raw = ltr507_write_raw,
	.attrs = &ltr301_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ltr301_info = {
	.read_raw = ltr507_read_raw,
	.write_raw = ltr507_write_raw,
	.attrs = &ltr301_attribute_group,
	.read_event_value	= &ltr507_read_event,
	.write_event_value	= &ltr507_write_event,
	.read_event_config	= &ltr507_read_event_config,
	.write_event_config	= &ltr507_write_event_config,
	.driver_module = THIS_MODULE,
};

static struct ltr507_chip_info ltr507_chip_info_tbl[] = {
	[ltr507] = {
		.partid = 0x09,
		.als_gain = ltr507_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr507_als_gain_tbl),
		.ps_gain = ltr507_ps_gain_tbl,
		.ps_gain_tbl_size = ARRAY_SIZE(ltr507_ps_gain_tbl),
		.als_mode_active = BIT(1),
		.als_gain_mask = BIT(3) | BIT(4),
		.als_gain_shift = 3,
		.info = &ltr507_info,
		.info_no_irq = &ltr507_info_no_irq,
		.channels = ltr507_channels,
		.no_channels = ARRAY_SIZE(ltr507_channels),
	},
	[ltr559] = {
		.partid = 0x09,
		.als_gain = ltr559_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr559_als_gain_tbl),
		.ps_gain = ltr559_ps_gain_tbl,
		.ps_gain_tbl_size = ARRAY_SIZE(ltr559_ps_gain_tbl),
		.als_mode_active = BIT(1),
		.als_gain_mask = BIT(2) | BIT(3) | BIT(4),
		.als_gain_shift = 2,
		.info = &ltr507_info,
		.info_no_irq = &ltr507_info_no_irq,
		.channels = ltr507_channels,
		.no_channels = ARRAY_SIZE(ltr507_channels),
	},
	[ltr301] = {
		.partid = 0x08,
		.als_gain = ltr507_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr507_als_gain_tbl),
		.als_mode_active = BIT(0) | BIT(1),
		.als_gain_mask = BIT(3),
		.als_gain_shift = 3,
		.info = &ltr301_info,
		.info_no_irq = &ltr301_info_no_irq,
		.channels = ltr301_channels,
		.no_channels = ARRAY_SIZE(ltr301_channels),
	},
};

static int ltr507_write_contr(struct ltr507_data *data, u8 als_val, u8 ps_val)
{
	int ret;
	ret = i2c_smbus_write_byte_data(data->client, LTR507_ALS_CONTR, als_val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(data->client, LTR507_PS_CONTR, ps_val);
}

static irqreturn_t ltr507_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ltr507_data *data = iio_priv(indio_dev);
	u32 buf[8];
	__le32 als = 0;
	u8 mask = 0;
	int j = 0;
	int ret;

	memset(buf, 0, sizeof(buf));

	/* figure out which data needs to be ready */
	if (test_bit(0, indio_dev->active_scan_mask) ||
	    test_bit(1, indio_dev->active_scan_mask))
		mask |= LTR507_STATUS_ALS_RDY;
	if (test_bit(2, indio_dev->active_scan_mask))
		mask |= LTR507_STATUS_PS_RDY;

	ret = ltr507_drdy(data, mask);
	if (ret < 0)
		goto done;

	if (mask & LTR507_STATUS_ALS_RDY) {
		ret = i2c_smbus_read_i2c_block_data(data->client,
			LTR507_ALS_DATA1, 3, (u8 *)&als);
		if (ret < 0)
			return ret;
		if (test_bit(0, indio_dev->active_scan_mask))
			buf[j++] = le32_to_cpu(als);
		if (test_bit(1, indio_dev->active_scan_mask))
			buf[j++] = le32_to_cpu(als);
	}

	if (mask & LTR507_STATUS_PS_RDY) {
		ret = i2c_smbus_read_word_data(data->client, LTR507_PS_DATA);
		if (ret < 0)
			goto done;
		buf[j++] = ret & LTR507_PS_DATA_MASK;
	}

	iio_push_to_buffers(indio_dev, (unsigned char*)&buf);

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t ltr507_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ltr507_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_PS_STATUS);
	if (ret < 0)
		return ret;
		
	if (ret < 0) {
		dev_err(&data->client->dev,
			"irq read int reg failed\n");
		return IRQ_HANDLED;
	}

	if (ret & LTR507_STATUS_ALS_INTR)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns());

	if (ret & LTR507_STATUS_PS_INTR)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns());

	return IRQ_HANDLED;
}

static int ltr507_init(struct ltr507_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_ALS_CONTR);
	if (ret < 0)
		return ret;

	data->als_contr = ret | data->chip_info->als_mode_active;

	ret = i2c_smbus_read_byte_data(data->client, LTR507_PS_CONTR);
	if (ret < 0)
		return ret;

	data->ps_contr = ret | LTR507_CONTR_ACTIVE;

	ret = ltr507_read_intr_prst(data, IIO_INTENSITY, &data->als_period);
	if (ret < 0)
		return ret;

	ret = ltr507_read_intr_prst(data, IIO_PROXIMITY, &data->ps_period);
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
		
	return ltr507_write_contr(data, data->als_contr, data->ps_contr);
}

static int ltr507_powerdown(struct ltr507_data *data)
{
	return ltr507_write_contr(data, data->als_contr &
				  ~data->chip_info->als_mode_active,
				  data->ps_contr & ~LTR507_CONTR_ACTIVE);
}

static int ltr507_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ltr507_data *data;
	struct iio_dev *indio_dev;
	int ret, partid, chip_idx = 0;
	const char *name = NULL;

	dev_info(&client->dev, "start %s sensor probing.\n", id->name);

	indio_dev = iio_device_alloc(sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock_als);
	mutex_init(&data->lock_ps);

	partid = i2c_smbus_read_byte_data(data->client, LTR507_PART_ID);
	if (partid < 0) {
		dev_err(&client->dev, "read part id error %d\n", partid);
		return ret;
	}

	if (id) {
		name = id->name;
		chip_idx = id->driver_data;
	} else {
		return -ENODEV;
	}

	dev_info(&client->dev, "driver name (%s) chip index (%d)\n", name, chip_idx);

	data->chip_info = &ltr507_chip_info_tbl[chip_idx];

	if ((partid >> 4) != data->chip_info->partid) {
		dev_err(&client->dev, "part id check error %d\n", partid);
		return -ENODEV;
	}

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = data->chip_info->info;
	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->no_channels;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ltr507_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "ltr507_init error %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "interrupt (%d)\n", client->irq);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, ltr507_interrupt_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"ltr507_thresh_event",
						indio_dev);
		if (ret) {
			dev_err(&client->dev, "request irq (%d) failed\n",
				client->irq);
			return ret;
		}
	} else {
		indio_dev->info = data->chip_info->info_no_irq;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ltr507_trigger_handler, NULL);
	if (ret)
		goto powerdown_on_error;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_buffer;

	dev_info(&client->dev, "%s sensor found.\n", id->name);

	return 0;

error_unreg_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
powerdown_on_error:
	ltr507_powerdown(data);
	return ret;
}

static int ltr507_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	ltr507_powerdown(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ltr507_suspend(struct device *dev)
{
	struct ltr507_data *data = iio_priv(i2c_get_clientdata(
					    to_i2c_client(dev)));
	return ltr507_powerdown(data);
}

static int ltr507_resume(struct device *dev)
{
	struct ltr507_data *data = iio_priv(i2c_get_clientdata(
					    to_i2c_client(dev)));

	return ltr507_write_contr(data, data->als_contr,
		data->ps_contr);
}
#endif

static SIMPLE_DEV_PM_OPS(ltr507_pm_ops, ltr507_suspend, ltr507_resume);

static const struct of_device_id ltr507_of_match[] = {
	{ .compatible = "liteon,ltr507" },
	{},
};
MODULE_DEVICE_TABLE(of, ltr507_of_match);

static const struct i2c_device_id ltr507_id[] = {
	{ "ltr507", ltr507},
	{ "ltr559", ltr559},
	{ "ltr301", ltr301},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltr507_id);

static struct i2c_driver ltr507_driver = {
	.driver = {
		.name   = LTR507_DRV_NAME,
		.pm	= &ltr507_pm_ops,
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(ltr507_of_match),
	},
	.probe  = ltr507_probe,
	.remove	= ltr507_remove,
	.id_table = ltr507_id,
};

module_i2c_driver(ltr507_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Lite-On LTR507 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");
