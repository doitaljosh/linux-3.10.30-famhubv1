/**
 * Copyright (C) 2013 Samsung R&D Institute India - Delhi.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * tas.c : tas Audio Amp driver
 *
 * @file tas.c
 * @brief  tas Audio Amp driver.
 * @author Dronamraju Santosh Pavan Kumar <dronamraj.k@samsung.com>
 * @date   2013/08/31
 */

#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/version.h>
#include <sound/soc.h>
#include <linux/t2d_print.h>
#include <uapi/sound/snd_amp.h>
//GOLF KERNEL #include <t2ddebugd.h>

static struct i2c_client *tas_client;

#define ON  1
#define OFF 0

enum peq_type {
	PEQ_TYPE_OFF,
	PEQ_TYPE_48K,
	PEQ_TYPE_44K,
	PEQ_TYPE_32K
};

enum mute_type {
	MUTE_TYPE_SOFT,
	MUTE_TYPE_HARD
};



/* global variables */
unsigned char volume_default;
unsigned char mute ;
unsigned int peq_checksum ;
unsigned int local_peq_checksum;
unsigned char amp_eq;
unsigned char amp_scale;
unsigned char amp_headphone;



/**
 * @brief This function internally uses i2c_master_send and i2c_master_recv.
 *        i2c_master_send - issue a single I2C message in master transmit mode.
 *        i2c_master_recv -  issue a single I2C message in master receive mode.
 *
 * @param [in] reg Register address.
 * @param [in] pdata  pointer to buffer containing data to be read
 *                     from Register.
 *
 * @return On success Retunrs the number of bytes written.
 *         On failure Returns negative error number.
 */
static int tas_i2c_read8(unsigned char reg, unsigned char *pdata)
{
	int ret;

	ret = i2c_master_send(tas_client, &reg, 1);
	if (ret < 1) {
		dev_err(&tas_client->dev, "%s: i2c write failed (%d)\n",
			__func__, ret);
		return ret < 0 ? ret : -EIO;
	}

	ret |= i2c_master_recv(tas_client, pdata, 1);
	if (ret < 1) {
		dev_err(&tas_client->dev, "%s: i2c read failed (%d)\n",
			__func__, ret);
		return ret < 0 ? ret : -EIO;
	}

	usleep_range(1000, 2000);

	return 0;
}

/**
 * @brief This function internally uses i2c_master_send function.
 *        i2c_master_send - issue a single I2C message in master transmit mode.
 *
 * @param [in] reg Register address.
 * @param [in] data Data that will be written to the register.
 *
 * @return On success Retunrs the number of bytes written.
 *         On failure Returns negative error number.
 */
static int tas_i2c_write8(unsigned char reg, unsigned char data)
{
	int ret;
	unsigned char buf[2] = {reg, data};

	ret = i2c_master_send(tas_client, buf, 2);
	if (ret < 2) {
		dev_err(&tas_client->dev, "%s: i2c write failed (%d)\n",
			__func__, ret);
		return ret < 0 ? ret : -EIO;
	}

	usleep_range(1000, 2000);
	return 0;
}

/**
 * @brief This function internally uses i2c_master_send function to transfer
 *        4 bytes of data.
 *        i2c_master_send - issue a single I2C message in master transmit mode.
 *
 * @param [in] reg Register address.
 * @param [in] data Data that will be written to the register.
 *
 * @return On success Retunrs the number of bytes written.
 *         On failure Returns negative error number.
 */
static int tas_i2c_write32(unsigned char reg, unsigned int  data)
{
	int ret;
	unsigned char buf[5] = {0,};

	buf[0] = reg;
	buf[1] = (data >> 24) & 0xff;
	buf[2] = (data >> 16) & 0xff;
	buf[3] = (data >> 8) & 0xff;
	buf[4] = data & 0xff;

	ret = i2c_master_send(tas_client, buf, 5);
	if (ret < 5) {
		dev_err(&tas_client->dev, "%s: i2c write failed (%d)\n",
			__func__, ret);
		return ret < 0 ? ret : -EIO;
	}

	usleep_range(1000, 2000);
	return 0;
}

/**
 * @brief Amplifier mute setting.
 *
 * @param [in] mute_b value (0 or 1).
 * @param [in] mute_type whether Auto mute or Soft mute.
 *
 * @return  On success Returns ret.
 * @return  On failure Returns negative error number.
 */
int tas_set_mute(bool mute_b, enum mute_type mutetype)
{
	int ret = 0;

	if (mute_b) {
		switch (mutetype) {
		case MUTE_TYPE_HARD:
			break;

		case MUTE_TYPE_SOFT:
			break;
		}
	} else {
	}


	return ret;
}
EXPORT_SYMBOL(tas_set_mute); // for power manager

/***Mute Volume ***/
static int snd_info_mute(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

/**
 * @brief snd_get_mute read the amplifier mute setting.
 *
 *  @param [in] kcontrol The control for the amplifier mute.
 *  @param [in] ucontrol The value that needs to be updated.
*/
static int snd_get_mute(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mute;
	return 0;
}

/**
 * @brief snd_put_h_mute - set the amplifier mute setting.
 *
 * @param [in] kcontrol The control for the amplifier mute.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_h_mute(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute_temp = 0;

	t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	mute_temp = ucontrol->value.integer.value[0];
	ret |= tas_set_mute(mute_temp, MUTE_TYPE_HARD);

	return ret;
}

/**
 * @brief snd_put_s_mute - set the amplifier mute setting.
 *
 * @param [in] kcontrol The control for the amplifier mute.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_s_mute(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute_temp = 0;

	t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	mute_temp = ucontrol->value.integer.value[0];
	ret |= tas_set_mute(mute_temp, MUTE_TYPE_SOFT);

	return ret;
}

static int tas_set_peq(enum peq_type peqtype)
{

	return 0;
}
/**
 * @brief Amplifier Scale settings.
 *
 * @remarks Factory data interface to set the actual.
 * @param [in] amp_scale scaling value.
 *
 * @return On success returns ret.
 *         On failure returns negative error number.
 */
static int tas_set_amp_scale(unsigned char amp_scale)
{
	int ret = 0;
	return ret;
}

/*** Amplifier Scale ***/
static int snd_info_amp_scale(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

/**
 * @brief snd_get_amp_scale - Read the amplifier scale setting.
 *
 * @param [in] kcontrol The control for the amplifier scale.
 * @param [in] ucontrol The value that needs to be updated.
 */
static int snd_get_amp_scale(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = amp_scale;
	return 0;
}

/**
 * @brief snd_put_amp_scale - set the amplifier scale setting.
 *
 * @param [in] kcontrol The control for the amplifier scale.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_amp_scale(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int amp_scale_temp = 0;

	//t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	amp_scale_temp = ucontrol->value.integer.value[0];
	ret |= tas_set_amp_scale(amp_scale_temp);

	if (ret == 0)
		amp_scale = amp_scale_temp;

	return ret;
}

/**
 * @brief Amplifier volume settings.
 *
 * @remarks Factory data interface to set the actual.
 * @param [in] amp_volume volume.
 *
 * @return On success Returns ret.
 *         On failure Returns negative error number.
 */
static int tas_set_amp_volume(unsigned char amp_volume)
{
	int ret = 0;

	return ret;
}

/*** Amplifier Volume ***/
static int snd_info_amp_volume(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

/**
 * @brief snd_get_amp_volume - Read the amplifier volume setting.
 *
 * @param [in] kcontrol The control for the amplifier volume.
 * @param [in] ucontrol The value that needs to be updated.
 */
static int snd_get_amp_volume(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = volume_default;
	return 0;
}

/**
 * @brief snd_put_amp_volume - set the amplifier volume setting.
 *
 * @param [in] kcontrol The control for the amplifier volume.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_amp_volume(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	unsigned char amp_vol;

	//t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	amp_vol = ucontrol->value.integer.value[0];
	ret |= tas_set_amp_volume(amp_vol);
	return ret;
}

/**
 * @brief Amplifier EQ settings.
 *
 * @remarks Factory data interface to set the actual.
 * @param [in] set_value settings.
 *
 * @return On success Returns ret.
 *         On failure Returns negative error number.
 */
static int tas_set_amp_eq(int set_value)
{
	int ret = 0;
	return ret;
}

/*** Amplifier Equalizer ***/
static int snd_info_amp_eq(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

/**
 * @brief snd_get_amp_eq - Read the amplifier equalizer setting.
 *
 * @param [in] kcontrol The control for the amplifier equalizer.
 * @param [in] ucontrol The value that needs to be updated.
 */
static int snd_get_amp_eq(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = amp_eq;
	return 0;
}

/**
 * @brief snd_put_amp_eq - set the amplifier equalizer setting.
 *
 * @param [in] kcontrol The control for the amplifier equalizer.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_amp_eq(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int amp_eq_temp = 0;

	t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	amp_eq_temp = ucontrol->value.integer.value[0];
	ret |= tas_set_amp_eq(amp_eq_temp);

	if (ret == 0)
		amp_eq = amp_eq_temp;

	return ret;
}
/**
 * @brief Amplifier Scale settings.
 *
 * @remarks Factory data interface to set the actual.
 * @param [in] headphone_gain gain value.
 *
 * @return On success returns ret.
 *         On failure returns negative error number.
 **/
static int tas_set_amp_headphone_gain(unsigned char headphone_gain)
{
	int ret = 0;
	return ret;
}

/***Amplifier Headphone gain control***/
static int snd_info_amp_hp(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 15;
	return 0;
}

/**
 * @brief snd_get_amp_hp - Read the amplifier headphone setting.
 *
 * @param [in] kcontrol The control for the amplifier headphone.
 * @param [in] ucontrol The value that needs to be updated.
 */
static int snd_get_amp_hp(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = amp_headphone;
	return 0;
}

/**
 * @brief snd_put_amp_eq - set the amplifier headphone gain setting.
 *
 * @param [in] kcontrol The control for the amplifier equalizer.
 * @param [in] ucontrol The value that needs to be set.
 */
static int snd_put_amp_hp(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int amp_headphone_temp = 0;

	//t2d_print(debug_level, "AMP %s %ld [pid:%d]\n", __FUNCTION__, ucontrol->value.integer.value[0], current->pid);

	amp_headphone_temp = ucontrol->value.integer.value[0];
	ret |= tas_set_amp_headphone_gain(amp_headphone_temp);
	if (ret == 0)
		amp_headphone = amp_headphone_temp;
	return ret;
}


/**
 * @brief Returns the status of tas amplifier chip.
 *
 * @param [in] void It takes no arguments.
 *
 * @return On success Returns 1 or 0.
 *         On failure Returns negative error number.
 */
int tas_get_amp_status(void)
{
	return 0;
}

/* Definition of controls. */
static struct snd_kcontrol_new snd_tas_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "mute",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_mute,
		.get   = snd_get_mute,
		.put   = snd_put_h_mute
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "mute-hard",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_mute,
		.get   = snd_get_mute,
		.put   = snd_put_h_mute
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "mute-soft",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_mute,
		.get   = snd_get_mute,
		.put   = snd_put_s_mute
	},
	{
		.iface =  SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "amp-scale",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_amp_scale,
		.get   = snd_get_amp_scale,
		.put   = snd_put_amp_scale
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "amp-volume",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_amp_volume,
		.get   = snd_get_amp_volume,
		.put   = snd_put_amp_volume
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "amp-eq",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_amp_eq,
		.get   = snd_get_amp_eq,
		.put   = snd_put_amp_eq
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name  = "amp-hp",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info  = snd_info_amp_hp,
		.get   = snd_get_amp_hp,
		.put   = snd_put_amp_hp
	},
};

/* Ioctl implementation */
static int snd_amp_control_ioctl(struct snd_card *card,
					struct snd_ctl_file *control,
					unsigned int cmd, unsigned long arg)
{

	return 0;
}

static int tas_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = 0;
	unsigned char data, index, addr;
	unsigned int data_32;
	amp_scale = 0x68;
	volume_default = 0xBF;
	peq_checksum = 0xFFFFFFFF;
	local_peq_checksum = 0xFFFFFFFF;
	mute = OFF;
	amp_eq = OFF;
	amp_headphone = 0x04;
	/*amp_wf = OFF;*/
	tas_client = client;
	dev_info(&tas_client->dev, "tas_probe & init start...\n");

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&tas_client->dev,
			"tas_probe failed! Line at %d\n", __LINE__);
		return -EIO;
	}


	ret |= tas_i2c_read8(0x01, &data);
	if (data == 0x00) {
		dev_info(&tas_client->dev,
			"tas_probe chip id is identified\n");
	} else {
		dev_err(&tas_client->dev,
			"tas_probe failed! chip id does not match. "
			"line at %d\n", __LINE__);
		return -EIO;
	}

	dev_info(&tas_client->dev, "tas_probe & init done.");

	return ret;
}

/**
 * @brief This function undo the all probing operations.
 *
 * @param [in] client i2c client for tas amplifier.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
static int tas_remove(struct i2c_client *client)
{
	dev_info(&tas_client->dev, "tas_remove... %d\n", __LINE__);
	snd_ctl_unregister_ioctl(snd_amp_control_ioctl);
	return 0;
}

static const struct of_device_id tas_of_match[] = {
	{ .compatible = "tas" },
	{}
};
MODULE_DEVICE_TABLE(of, tas_of_match);

/**
 * Devices are identified using device id
 * of the chip
 */
static const struct i2c_device_id tas_i2c_id[] = {
	{ "tas", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas_i2c_id);

/**
 * During initialization, it registers a probe() method, which the I2C core
 * invokes when an associated host controller is detected.
 */
static struct i2c_driver tas_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tas",
		.of_match_table = of_match_ptr(tas_of_match),
	},
	.id_table = tas_i2c_id,
	.probe = tas_probe,
	.remove = tas_remove,
};
module_i2c_driver(tas_driver);


/* Device initialization routine */
int tas_audio_amplifier_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_card *card = runtime->card->snd_card;
	struct snd_kcontrol *kcontrol;
	unsigned int index;
	int err;

	dev_info(&tas_client->dev,
			"tas_audio_amplifier_init start.\n");
	snd_ctl_register_ioctl(snd_amp_control_ioctl);

	for (index = 0; index < ARRAY_SIZE(snd_tas_controls); index++) {
		/* NULL can be replaced by data that we want in callback. */
		kcontrol = snd_ctl_new1(&snd_tas_controls[index], NULL);
		err = snd_ctl_add(card, kcontrol);
		if (err < 0)
			return err;
	}
#ifdef CONFIG_T2DDEBUGD
	t2d_dbg_register("tas_amplifier debug",
				4, t2ddebug_tas_amp_debug, NULL);
#endif /*CONFIG_T2DDEBUGD*/

	return 0;
}
EXPORT_SYMBOL(tas_audio_amplifier_init);


