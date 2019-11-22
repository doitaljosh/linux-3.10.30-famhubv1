/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * Copyright (C) 2013 Samsung R&D Institute India-Delhi.
 * Author: Dronamraju Santosh Pavan Kumar <dronamraj.k@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * @file eeprom_s24c512c.c
 * @brief Driver for the S24C512C eeprom chip.
 * @author Dronamraju Santosh Pavan Kumar <dronamraj.k@samsung.com>
 * @date   2013/07/30
 *
 */

#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/eeprom.h>
#include <linux/vmalloc.h>

#define true 1
#define false 0

/* eeprom specific macros */
#define EEP_PAGE_SIZE 		128
#define NUM_OF_PAGES 		512
#define EEPROM_ADDR_SIZE 	2
#define EEPROM_MAX_SIZE 	65536
#define MAX_ATTR_BYTES 		6

/* 10 milliseconds of delay is required after each i2c write operation */
#define EEP_WDELAY 	10000

#define EEP_WP_ON	1
#define EEP_WP_OFF	0

/* cache parameters */
unsigned char cache_flag[NUM_OF_PAGES] = {false};
unsigned char *cache_buf;

/* upper/lower 8-bit word address macros */
#define HI_BYTE(x) (((x) >> 8) & (0x00FF))
#define LO_BYTE(x) ((x) & (0x00FF))

/* define global mutex statically ***/
static DEFINE_MUTEX(eep_mutex);
static DEFINE_MUTEX(eep_dev_mutex);

/* global variables for this module ***/
int counter;
static const unsigned int eeprom_size = EEPROM_MAX_SIZE;
static int eeprom_dev_major;
static struct i2c_client *eep_client;
static unsigned long last_write_time;
static int eeprom_wp_state;

static void eep_wp_control(int on_off)
{
	switch(on_off) {
	case EEP_WP_ON:
		usleep_range(10, 20);
#ifdef CONFIG_ARCH_SDP1406
		eep_set_wp(0);
#else
		eep_set_wp(1);
#endif
		break;
	case EEP_WP_OFF:
#ifdef CONFIG_ARCH_SDP1406
		eep_set_wp(1);
#else
		eep_set_wp(0);
#endif
		usleep_range(10, 20);
		break;
	default:
		break;
	}

	eeprom_wp_state = on_off;
}

/**
 * @brief This function internally uses i2c_master_send function.
 *        i2c_master_send - issue a single I2C message in master transmit mode.
 *
 * @param [in] buf Data that will be written to the eeprom.
 * @param [in] len How many bytes to write.
 *
 * @return On success Retunrs the number of bytes written.
 *         On failure Returns negative error number.
 */
int eep_i2c_write(const unsigned char *buf, int len)
{
	int ret = 0;
	unsigned long write_delayed = 0;

	if (!buf || len <= 0) {
		eep_err(KERN_ERR "[eep_i2c_write] inavalid input.\n");
		return -EFAULT;
	}
	if (eep_client) {
		if (last_write_time) {
			write_delayed = jiffies_to_usecs(jiffies - last_write_time);
			if (write_delayed < EEP_WDELAY)
				usleep_range((EEP_WDELAY - write_delayed), (EEP_WDELAY - write_delayed + 50));
		}

		ret = i2c_master_send(eep_client, (const char *)buf, len);
		last_write_time = jiffies;
	} else {
		eep_err(KERN_ERR "[eep_i2c_write] eeprom dev is not probed.\n");
		ret = -ENODEV;
	}
	return ret;
}
EXPORT_SYMBOL(eep_i2c_write);

/**
 * @brief This function internally user the i2c_transfer to perform read
 *        operation on eeprom.
 *
 * @param [in] reg current offset.
 * @param [in] buf pointer to buffer containing data to be read from eeprom.
 * @param [in] len number of bytes to read.
 *
 * @return On Success Returns no of messages executed by i2c_transfer.
 *         On failure Returns negative error number.
 */
int eep_reg_read(unsigned int reg, unsigned char *buf, int len)
{
	int ret = 0;
	struct i2c_adapter *adap;
	struct i2c_msg msg[2];
	unsigned char buf_register[EEPROM_ADDR_SIZE] = {HI_BYTE(reg),
								LO_BYTE(reg)};
	unsigned long write_delayed = 0;

	if (!buf || len <= 0) {
		eep_err(KERN_ERR "[eep_reg_read] inavalid input.\n");
		return -EFAULT;
	}
	if (eep_client) {
		adap = eep_client->adapter;
		msg[0].addr = eep_client->addr;
		msg[0].flags = eep_client->flags & I2C_M_TEN;
		msg[0].len = EEPROM_ADDR_SIZE;
		msg[0].buf = buf_register;
		msg[1].addr = eep_client->addr;
		msg[1].flags = eep_client->flags & I2C_M_TEN;
		msg[1].flags |= I2C_M_RD;
		msg[1].len = len;
		msg[1].buf = buf;

		if (last_write_time) {
			write_delayed = jiffies_to_usecs(jiffies - last_write_time);
			if (write_delayed < EEP_WDELAY)
				usleep_range((EEP_WDELAY - write_delayed), EEP_WDELAY);
			last_write_time = 0;
		}

		/* i2c_transfer returns negative errno, else the number of
		 * messages executed.
		 */
		ret = i2c_transfer(adap, msg, EEPROM_ADDR_SIZE);
		if (ret < 0)
			eep_err(KERN_ERR "[eep_reg_read] i2c read error!!\n");
	} else {
		eep_err(KERN_ERR "[eep_reg_read] eeprom dev is not probed.\n");
		ret = -ENODEV;
	}
	return ret;
}
EXPORT_SYMBOL(eep_reg_read);

/**
 * @brief This function is the software implementation for memory and
 *        it provides fast read access. Which maintains the replica data of
 *        the eeprom memory. If user request for data from the eeprom,
 *        the data is returned from the memory if the flag
 *        in cache_buf is enabled(set to 1).If the flag was not set,
 *        then it reads the memory from the eeprom  and this data is
 *        copied in to memory, then retuns the data from the
 *        memory.
 *
 * @param [in] addr Start address from which application intend to
 *                  read the data.
 * @param [in] read_buf pointer to buffer containing data to be read from
 *                      cache memory.
 * @param [in] count Number of bytes application intend to read.
 *
 * @return On Success Returns number of bytes red.
 *         On Failure Returns negative error number.
 */
ssize_t eep_cache_read(unsigned int addr, unsigned char *read_buf, size_t count)
{
	unsigned int i;
	unsigned int read_start;
	unsigned int page_offset = addr / EEP_PAGE_SIZE;
	unsigned int page_remain = addr % EEP_PAGE_SIZE;
	unsigned int page_count = ((page_remain + count) / EEP_PAGE_SIZE) + 1;
	eep_log(KERN_INFO "[eep_cache_read] initiated.\n");

	if (!read_buf || count <= 0) {
		eep_err(KERN_ERR "[eep_cache_read] inavalid input.\n");
		return -EFAULT;
	}

	/* exception case for count */
	if (((page_remain + count) % EEP_PAGE_SIZE) == 0)
		page_count--;

	for (i = 0; i < page_count; i++) {
		/* If data is not present, calculate address,bring it into
		 * cache_buf, make the correspoding flag true, copy the
		 * data into read_buf
		 */
		if (!cache_flag[page_offset + i]) {
			read_start = (page_offset + i) * (EEP_PAGE_SIZE);
			if (eep_reg_read(read_start, &cache_buf[read_start],
							EEP_PAGE_SIZE) < 0) {
				eep_err(KERN_ERR
				"[eep_cache_read] eep_reg_read failed.\n");
				return -EFAULT;
			}
			cache_flag[page_offset+i] = true;
		}
	}
	/* copy memory to read buffer */
	memcpy(read_buf, &cache_buf[addr], count);
	eep_log(KERN_INFO
		"[eep_cache_read] data is copied from_buf to read_buf.\n");
	return count;
}

/**
 * @brief This function is the software implementation for memory.
 *        It compare the data in memory and buffer, if data is same,
 *        eeprom write operation is not performed at specified location.
 *        if not same then it will perform the write operation.
 *
 * @param [in] addr Start address from which application intend to
 *                  write the data.
 * @param [in] buf pointer to buffer containing data to be written to eeprom.
 * @param [in] count Count to the number of data bytes intend to write.
 *
 * @return On Success Retuns number of bytes written.
 *         On failure Returns negative error number.
 */
ssize_t eep_cache_write(unsigned int addr, unsigned char *write_buf,
								size_t count)
{
	int ret = 0;
	unsigned int i;
	unsigned int read_start;
	/* Page Offset */
	unsigned int page_offset = addr / EEP_PAGE_SIZE;
	unsigned int page_remain = addr % EEP_PAGE_SIZE;
	/* page count */
	unsigned int page_count = ((page_remain + count)
							  / EEP_PAGE_SIZE) + 1;

	eep_log(KERN_INFO "[eep_cache_write] initiated.\n");

	/* exception case for count */
	if (((page_remain + count) % EEP_PAGE_SIZE) == 0)
		page_count--;

	if (!write_buf || count <= 0) {
		eep_err(KERN_ERR "[eep_cache_write] inavalid input.\n");
		return -EFAULT;
	}
	for (i = 0; i < page_count; i++) {
		/*check the flag*/
		if (!cache_flag[page_offset+i]) {
			read_start = (page_offset + i) * EEP_PAGE_SIZE;
			if (eep_reg_read(read_start, &cache_buf[read_start],
							EEP_PAGE_SIZE) < 0) {
				eep_err(KERN_ERR
				"[eep_cache_write] eep_reg_read failed.\n");
				return -EFAULT;
			}
			/*set the flag */
			cache_flag[page_offset + i] = true;
		}
	}
	/*comparing buffer data and write buffer data  */
	if (memcmp(&cache_buf[addr], write_buf, count) == 0) {
		eep_log(KERN_INFO
		"Cache Check.. Same Write Data :Write operation will Abort.\n");
		return -EPERM;
	}
	return ret;
}

/**
 * @brief This function reads the data from eeprom device and it can be used by
 *        internal drivers.
 *
 * @param [in] buf pointer to buffer containing data to be read from eeprom.
 * @param [in] count Number of bytes application/driver intend to read.
 * @param [in] addr offset position.
 *
 * @return On success Returns count(number of read bytes).
 *         On failure Returns negative error number.
 */
ssize_t eeprom_dev_read(char *pbuf, size_t count, unsigned long addr)
{
	int ret = 0;

	eep_log(KERN_INFO "[eeprom_dev_read] Read.\n");

	if (!pbuf) {
		eep_err(KERN_ERR "[eeprom_dev_read] buffer has no memory.\n");
		ret = -EFAULT;
		goto out_ext;
	}
	if (count > 0) {
		mutex_lock(&eep_dev_mutex);
		if (addr + count > eeprom_size) {
			eep_err(KERN_ERR "[eeprom_dev_read] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		/*If there is a  buffer allocation, read from cache*/
		if (cache_buf != NULL) {
			if ((eep_cache_read(addr, pbuf, count)) < 0) {
				eep_err(KERN_ERR
				"[eeprom_dev_read] eep_cache_read failed.\n");
				ret = -EFAULT;
				goto out;
			}
		/*If there is no  buffer allocation, read from device*/
		} else if ((eep_reg_read(addr, pbuf, count)) < 0) {
			eep_err(KERN_ERR
				"[eeprom_dev_read] eep_reg_read failed.\n");
			ret = -EFAULT;
			goto out;
		}
		ret = count;
	} else {
		eep_err(KERN_ERR "[eeprom_dev_read] inavalid count.\n");
		ret = -EFAULT;
		goto out_ext;
	}

out:
	mutex_unlock(&eep_dev_mutex);
out_ext:
	return ret;
}
EXPORT_SYMBOL(eeprom_dev_read);

/**
 * @brief This function writes the data to eeprom device and it can be used by
 *        internal drivers.
 *
 * @param [in] buf pointer to buffer containing data to be written to eeprom.
 * @param [in] count Number of bytes application/driver intend to write.
 * @param [in] addr offset position.
 *
 * @return On success Returns count(the number of bytes successfully written).
 *         On failure Returns negative error number.
 */
ssize_t eeprom_dev_write(char *pbuf, size_t count, unsigned long addr)
{
	int ret = 0;
	unsigned char temp_buf[EEPROM_ADDR_SIZE + EEP_PAGE_SIZE] = {0,};
	char *temp_pbuf = pbuf;
	int remainder_count;
	int temp_count;
	unsigned long addr_offset = addr;
#if defined(CONFIG_ARCH_SDP1404) && !defined(CONFIG_VD_RELEASE)
	unsigned int *vir_addr = NULL;
#endif

	eep_log(KERN_INFO "[eeprom_dev_write] Write.\n");

	if (!pbuf) {
		eep_err(KERN_ERR "[eeprom_dev_write] buffer has no memory.\n");
		ret = -EFAULT;
		goto out_ext;

	}
	if (count > 0) {
		mutex_lock(&eep_dev_mutex);
		if (addr + count > eeprom_size) {
			eep_err(KERN_ERR "[eeprom_dev_write] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		if (cache_buf != NULL) {
			if ((eep_cache_write(addr, pbuf, count)) < 0) {
				eep_log(KERN_INFO
				"[eeprom_dev_write] data already in eeprom.\n");
				ret = count;
				goto out;
			}
		}
		remainder_count = (int)count;

		eep_wp_control(EEP_WP_OFF);

		while (remainder_count > 0) {
			/* assign any value less than or equal to
			 * pagesize(128bytes) to temp_count
			 * because i2c_write can handle data
			 * up to page size. i2c_write should be
			 * perform on page by page.
			 */
			if ((((addr%EEP_PAGE_SIZE) + remainder_count)/EEP_PAGE_SIZE) > 0)
				temp_count = EEP_PAGE_SIZE - (addr%EEP_PAGE_SIZE);
			else
				temp_count = remainder_count;

			/*first two bytes reserve for address */
			temp_buf[0] = HI_BYTE(addr);
			temp_buf[1] = LO_BYTE(addr);
			memcpy(temp_buf+EEPROM_ADDR_SIZE, temp_pbuf,
								temp_count);
			if (eep_i2c_write(temp_buf,
					(temp_count + EEPROM_ADDR_SIZE)) < 0) {
				eep_err(KERN_ERR
				"[eeprom_dev_write] failed to i2c transfer.\n");
				ret = -EFAULT;

#if defined(CONFIG_ARCH_SDP1404) && !defined(CONFIG_VD_RELEASE)
				vir_addr = (unsigned int *)ioremap(0x1122b008, 4);
				if(vir_addr != NULL)
				{
					printk(KERN_CRIT "[eeprom_dev_write] RESETN_EDID=%08X\n", *vir_addr);
					iounmap(vir_addr);
					vir_addr = NULL;
				}

				vir_addr = (unsigned int *)ioremap(0x11250dd8, 8);
				if(vir_addr != NULL)
				{
					printk(KERN_CRIT "[eeprom_dev_write] P16.1 control=%08X, write=%08X\n", *vir_addr, *(vir_addr + 1));
					iounmap(vir_addr);
					vir_addr = NULL;
				}
#endif
				goto out;
			}
			addr += temp_count;
			temp_pbuf += temp_count;
			remainder_count -= temp_count;
		}
		memcpy(&cache_buf[addr_offset], pbuf, count);
			eep_log(KERN_INFO
		"[eeprom_dev_write] data is copied in to cache_memory.\n");
	} else {
		eep_err(KERN_ERR "[eeprom_dev_read] inavalid count.\n");
		ret = -EFAULT;
		goto out_ext;
	}
	ret = count;

out:
	if (eeprom_wp_state == EEP_WP_OFF)
		eep_wp_control(EEP_WP_ON);
	mutex_unlock(&eep_dev_mutex);
out_ext:
	return ret;
}
EXPORT_SYMBOL(eeprom_dev_write);

/** @brief This function inhibits the write operation on eeprom.
 *
 * @param [in] protect value(0 or 1).
 *             1 - To protect memory.
 *             0 - To unprotect memory.
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_set_wp(int protect)
{
	int ret = 0;
	static int gpio = -1;
//	static unsigned int cbus_reg = 0;
//	unsigned int tmp;
//	void __iomem *vbase;

	if (protect != 0 && protect != 1) {
		eep_err(KERN_ERR "[eep_set_wp] invalid input.\n");
		return -EINVAL;
	}
	if (eep_client) {
#ifdef CONFIG_ARCH_SDP1404
		if (!cbus_reg)
			of_property_read_u32(eep_client->dev.of_node,
				"samsung,cbusdetect-reg", &cbus_reg);
		if (cbus_reg) {
			vbase = ioremap(cbus_reg, 0x4);
			tmp = readl((void *)vbase);
			if (!(tmp & 0x40)) {
				tmp |= 0x40;
				writel(tmp, (void *)vbase);
			}
			iounmap(vbase);
		}
#endif
		if (gpio < 0) {
			gpio = of_get_named_gpio(eep_client->dev.of_node,
							"samsung,eeprom-wp", 0);
			eep_log("gpio[%d]\n", gpio);
			if (!gpio_is_valid(gpio)) {
				gpio = -1;
				eep_err(KERN_ERR "[eep_set_wp] fail to get gpio.\n");
				return -EPERM;
			}
		}
		ret = gpio_request(gpio, "eeprom-wp");
		if (ret) {
			if (ret == -EBUSY)
				eep_err(KERN_ERR "[eep_set_wp] gpio busy: %d\n",
									ret);
			else
				eep_err(KERN_ERR
				"[eep_set_wp] Can't request gpio: %d\n", ret);
			return ret;
		}
		gpio_direction_output(gpio, 0);
		gpio_set_value_cansleep(gpio, protect);
		gpio_free(gpio);
	} else {
		eep_err(KERN_ERR "[eep_set_wp] eeprom dev is not probed.\n");
		ret = -ENODEV;
	}
	return ret;
}
EXPORT_SYMBOL(eep_set_wp);

/**
 * @brief Returns the status of eeprom write protection.
 *
 * @param [in] void It takes no arguments.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_get_wp()
{
	int ret = 0;
	int gpio = 0;

	if (eep_client) {
		gpio = of_get_named_gpio(eep_client->dev.of_node,
						"samsung,eeprom-wp", 0);
		eep_log("gpio[%d]\n", gpio);

		if (!gpio_is_valid(gpio)) {
			eep_err(KERN_ERR "[eep_get_wp] fail to get gpio.\n");
			return -EPERM;
		}
		ret = gpio_request(gpio, "eeprom-wp");
		if (ret) {
			if (ret == -EBUSY)
				eep_err(KERN_ERR "[eep_get_wp] gpio busy: %d\n",
									ret);
			else
				eep_err(KERN_ERR
				"[eep_get_wp] Can't request gpio: %d\n", ret);
			return ret;
		}
		ret = gpio_get_value_cansleep(gpio);
		gpio_free(gpio);
	} else {
		eep_err(KERN_ERR "[eep_get_wp] eeprom dev is not probed.\n");
		ret = -ENODEV;
	}
	return ret;
}
EXPORT_SYMBOL(eep_get_wp);

/**
 * @brief This function resets the whole eeprom chip.
 *
 * @parms [in] void It takes no arguments.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
static int eep_reset(void)
{
	int ret = 0;
	unsigned char *init_buff = NULL;
	unsigned int i, addr_offset = 0;
	unsigned int remainder_count;

	eep_log(KERN_INFO "[eep_reset] Reset.\n");

	init_buff = vmalloc(EEP_PAGE_SIZE + EEPROM_ADDR_SIZE);
	if (!init_buff) {
		eep_err(KERN_ERR "[eep_reset] no memory.\n");
		ret = -ENOMEM;
		goto out_ext;
	}
	memset(init_buff+2, 0xFF, EEP_PAGE_SIZE);

	mutex_lock(&eep_dev_mutex);
	/* if modulus of eeprom_size and EEP_PAGE_SIZE is greater than zero
	 * then aasign it to remainder_count and pass it to eep_i2c_write
	 */
	remainder_count = (eeprom_size % EEP_PAGE_SIZE);

	eep_wp_control(EEP_WP_OFF);

	for (i = 0; i < (eeprom_size/EEP_PAGE_SIZE); i++) {
		/*first two bytes reserve for address*/
		init_buff[0] = HI_BYTE(addr_offset);
		init_buff[1] = LO_BYTE(addr_offset);
		if (eep_i2c_write(init_buff,
				     EEP_PAGE_SIZE + EEPROM_ADDR_SIZE) < 0) {
			eep_err(KERN_ERR
		"[eep_reset] failed to i2c transfer at page no: %d\n", i);
			ret = -EFAULT;
			goto out;
		}
		addr_offset += EEP_PAGE_SIZE;
		cache_flag[i] = false;
	}
	if (remainder_count) {
		init_buff[0] = HI_BYTE(addr_offset);
		init_buff[1] = LO_BYTE(addr_offset);
		if (eep_i2c_write(init_buff,
				     EEP_PAGE_SIZE + EEPROM_ADDR_SIZE) < 0) {
			eep_err(KERN_ERR
		"[eep_reset] failed to i2c transfer at page no: %d\n", i);
			ret = -EFAULT;
			goto out;
		}
		addr_offset += remainder_count;
		cache_flag[i] = false;
	}

out:
	eep_wp_control(EEP_WP_ON);

	vfree(init_buff);
	mutex_unlock(&eep_dev_mutex);
out_ext:
	return ret;
}

/**
 * @brief This function opens the eeprom device.
 *
 * @param inode inode.
 * @param [in] fp File pointer points to the file descriptor.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_open(struct inode *inode, struct file *fp)
{
	int ret = 0;

	eep_log(KERN_INFO "[eep_open] Open.\n");

	mutex_lock(&eep_mutex);
	counter++;
	eep_log(KERN_INFO "Number of times open() was called:%d\n", counter);
	eep_log(KERN_INFO "major number=%d, minor number=%d", imajor(inode),
							iminor(inode));
	eep_log(KERN_INFO "process id of the current process:%d\n",
							current->pid);
	mutex_unlock(&eep_mutex);
	return ret;
}

/**
 * @brief This function undo the all open call operations.
 *
 * @param inode inode.
 * @param [in] fp File pointer points to the file descriptor.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_close(struct inode *inode, struct file *fp)
{
	int ret = 0;

	eep_log(KERN_INFO "[eep_close] close.\n");

	mutex_lock(&eep_mutex);
	counter--;
	mutex_unlock(&eep_mutex);
	return ret;
}

/**
 * @brief The lseek method is used to change the current read/write position
 *        in a file
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] offset Offset.
 * @param [in] origin Origin.
 *
 * @return On success The new position(resulting offset) is returned.
 *         On failure Returns negative error number.
 */
loff_t eep_lseek(struct file *fp, loff_t offset, int origin)
{
	loff_t current_offset = 0;

	eep_log(KERN_INFO "[eep_lseek] lseek.\n");

	switch (origin) {
	case SEEK_SET:
		current_offset = offset;
		break;
	case SEEK_CUR:
		current_offset = fp->f_pos + offset;
		break;
	case SEEK_END:
		current_offset = eeprom_size - offset;
		break;
	default:
		break;
	}
	if (current_offset >= eeprom_size) {
		eep_err(KERN_WARNING "[eep_seek] offset overflow!\n");
		current_offset = eeprom_size - 1;
	} else if (current_offset < 0) {
		eep_err(KERN_WARNING "[eep_seek] offset underflow!\n");
		current_offset = 0;
	}

	fp->f_pos = current_offset;

	return current_offset;
}

/**
 * @brief This function reads the data from eeprom device.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] buf Pointer to the user space buffer.
 * @param [in] count Number of bytes application intend to read.
 * @param [in/out] pos Current file offset position.
 *
 * @return On success Returns count(number of read bytes).
 *         On failure Returns negative error number.
 */
ssize_t eep_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	int ret = 0;
	unsigned char buf_local[EEPROM_LOCAL_BUFF_SIZE], *mem_buf = NULL;
	unsigned char *pbuf_local = buf_local;
	loff_t current_offset = fp->f_pos;

	eep_log(KERN_INFO "[eep_read] Read.\n");
	if (!buf) {
		eep_err(KERN_ERR "User buf has no memory allocation.\n");
		ret = -EFAULT;
		goto out_ext;
	}
	if (count > 0) {
		mutex_lock(&eep_mutex);
		if (current_offset + count > eeprom_size) {
			eep_err(KERN_ERR "[eep_read] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		if (count > EEPROM_LOCAL_BUFF_SIZE) {
			mem_buf = vmalloc(count);
			if (!mem_buf) {
				eep_err(KERN_ERR
					"failed to allocate memory.\n");
				ret = -ENOMEM;
				goto out;
			}
			pbuf_local = mem_buf;
		}
		ret = eeprom_dev_read(pbuf_local, count, current_offset);
		if (ret < 0) {
			eep_err(KERN_ERR "[eep_read] read failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
		/*copy data to user*/
		if (copy_to_user(buf, pbuf_local, ret)) {
			eep_err(KERN_ERR "[eep_read] copy_to_user failed.\n");
			ret = -EFAULT;
			goto out_err;
		} else {
			current_offset += ret;
			goto out_err;
		}
	} else {
		eep_err(KERN_ERR "[eep_read] invalid count.\n");
		ret = -EFAULT;
		goto out_ext;
	}

out_err:
	if (mem_buf != NULL)
		vfree(mem_buf);
out:
	mutex_unlock(&eep_mutex);
out_ext:
	return ret;
}

/**
 * @brief This function writes the data to eeprom device.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] buf Pointer to the user space buffer.
 * @param [in] count Number of bytes application intend to write.
 * @param [in/out] pos Current file offset position.
 *
 * @return On success Returns count(the number of bytes successfully written).
 *         On failure Returns negative error number.
 */
ssize_t eep_write(struct file *fp, const char __user *buf,
						size_t count, loff_t *pos)
{
	int ret = 0;
	unsigned char buf_local[EEPROM_LOCAL_BUFF_SIZE], *mem_buf = NULL;
	unsigned char *pbuf_local = buf_local;
	loff_t current_offset = fp->f_pos;

	eep_log(KERN_INFO "[eep_write] eep write.\n");
	if (!buf) {
		eep_err(KERN_ERR "[eep_write] Invalid User buffer.\n");
		ret = -EFAULT;
		goto out_ext;
	}
	if (count > 0) {
		mutex_lock(&eep_mutex);
		if (current_offset + count > eeprom_size) {
			eep_err(KERN_ERR "[eep_write] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		if (count > EEPROM_LOCAL_BUFF_SIZE) {
			mem_buf = vmalloc(count);
			if (!mem_buf) {
				eep_err(KERN_INFO
					"failed to allocate memory.\n");
				ret = -ENOMEM;
				goto out;
			}
			pbuf_local = mem_buf;
		}
		/*copy data from user. */
		if (copy_from_user(pbuf_local, buf, count)) {
			eep_err(KERN_ERR
				"[eep_write] copy_from_user failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
		ret = eeprom_dev_write(pbuf_local, count, current_offset);
		if (ret < 0) {
			eep_err(KERN_ERR "[eep_write], Write failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
		current_offset += ret;
	} else {
		eep_err(KERN_ERR "[eep_write] inavalid count.\n");
		ret = -EFAULT;
		goto out_ext;
	}

out_err:
	if (mem_buf != NULL)
		vfree(mem_buf);
out:
	mutex_unlock(&eep_mutex);
out_ext:
	return ret;
}

static int eeprom_usr_write(const char __user *buf, size_t count, unsigned long addr)
{
	int ret = 0;
	unsigned char buf_local[EEPROM_LOCAL_BUFF_SIZE], *mem_buf = NULL;
	unsigned char *pbuf_local = buf_local;

	eep_log(KERN_INFO "[eeprom_usr_write] eep write.\n");
	if (!buf) {
		eep_err(KERN_ERR "[eep_write] Invalid User buffer.\n");
		ret = -EFAULT;
		goto out;
	}
	if (count > 0) {
		if (addr + count > eeprom_size) {
			eep_err(KERN_ERR "[eep_write] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		if (count > EEPROM_LOCAL_BUFF_SIZE) {
			mem_buf = vmalloc(count);
			if (!mem_buf) {
				eep_err(KERN_INFO
					"failed to allocate memory.\n");
				ret = -ENOMEM;
				goto out;
			}
			pbuf_local = mem_buf;
		}
		/*copy data from user. */
		if (copy_from_user(pbuf_local, buf, count)) {
			eep_err(KERN_ERR
				"[eep_write] copy_from_user failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
		ret = eeprom_dev_write(pbuf_local, count, addr);
		if (ret < 0) {
			eep_err(KERN_ERR "[eep_write], Write failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
	} else {
		eep_err(KERN_ERR "[eep_write] inavalid count.\n");
		ret = -EFAULT;
		goto out;
	}

out_err:
	if (mem_buf != NULL)
		vfree(mem_buf);
out:
	return ret;
}

ssize_t eeprom_usr_read(char __user *buf, size_t count, unsigned long addr)
{
	int ret = 0;
	unsigned char buf_local[EEPROM_LOCAL_BUFF_SIZE], *mem_buf = NULL;
	unsigned char *pbuf_local = buf_local;

	eep_log(KERN_INFO "[eeprom_usr_read] Read.\n");
	if (!buf) {
		eep_err(KERN_ERR "User buf has no memory allocation.\n");
		ret = -EFAULT;
		goto out;
	}
	if (count > 0) {
		if (addr + count > eeprom_size) {
			eep_err(KERN_ERR "[eep_read] overflow!!.\n");
			ret = -EFAULT;
			goto out;
		}
		if (count > EEPROM_LOCAL_BUFF_SIZE) {
			mem_buf = vmalloc(count);
			if (!mem_buf) {
				eep_err(KERN_ERR
					"failed to allocate memory.\n");
				ret = -ENOMEM;
				goto out;
			}
			pbuf_local = mem_buf;
		}
		ret = eeprom_dev_read(pbuf_local, count, addr);
		if (ret < 0) {
			eep_err(KERN_ERR "[eep_read] read failed.\n");
			ret = -EFAULT;
			goto out_err;
		}
		/*copy data to user*/
		if (copy_to_user(buf, pbuf_local, ret)) {
			eep_err(KERN_ERR "[eep_read] copy_to_user failed.\n");
			ret = -EFAULT;
		}
	} else {
		eep_err(KERN_ERR "[eep_read] invalid count.\n");
		ret = -EFAULT;
		goto out;
	}

out_err:
	if (mem_buf != NULL)
		vfree(mem_buf);
out:
	return ret;
}

/**
 * @brief This function performs control operations on eeprom.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] cmd Request command.
 * @param [in/out] args The arguments based on request command.
 *
 * @return On Success Returns zero.
 *         On failure Returns negative error number.
 */
long eep_ioctl(struct file *fp, unsigned int cmd, unsigned long args)
{
	int protect = 0, size;
	long ret = 0;

	eep_log(KERN_INFO "[eep_ioctl] ioctl.\n");

	/*verify args*/
	if (_IOC_TYPE(cmd) != EEPROM_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > EEPROM_MAX_CMDS)
		return -ENOTTY;
	if (_IOC_DIR(cmd) & _IOC_READ)
		if (!access_ok(VERIFY_WRITE, (void *)args, _IOC_SIZE(cmd)))
			return -EFAULT;
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		if (!access_ok(VERIFY_READ, (void *)args, _IOC_SIZE(cmd)))
			return -EFAULT;
	switch (cmd) {
	case EEPROM_RESET:
		eep_reset();
		break;
	case EEPROM_SET_WP:
		if (copy_from_user(&protect, (int *)args, sizeof(int))) {
			eep_err(KERN_ERR
				"[eep_ioctl] failed copy_from_user.\n");
			return -EFAULT;
		}
		eep_set_wp(protect);
		break;
	case EEPROM_GET_WP:
		protect = eep_get_wp();
		if (copy_to_user((int *)args, &protect, sizeof(int))) {
			eep_err(KERN_ERR
				"[eep_ioctl] failed copy_to_user.\n");
			ret =  -EFAULT;
		}
		break;
	case EEPROM_GET_SIZE:
		size = eeprom_size;
		if (copy_to_user((int *)args, &size, sizeof(int))) {
			eep_err(KERN_ERR
				"[eep_ioctl] failed copy_to_user.\n");
			ret = -EFAULT;
		}
		break;
	case EEPROM_WRITE_DATA:
	case EEPROM_READ_DATA:
		{
			struct eeprom_io_pkt pkt;
			if (copy_from_user(&pkt, (struct eeprom_io_pkt *)args, sizeof(pkt))) {
				eep_err(KERN_ERR
					"[eep_ioctl] failed copy_from_user.\n");
				ret = -EFAULT;
			}

			if (cmd == EEPROM_WRITE_DATA)
				ret = eeprom_usr_write(pkt.wbuf, pkt.size, pkt.addr);
			else
				ret = eeprom_usr_read(pkt.rbuf, pkt.size, pkt.addr);
		}
		break;
	default:
		eep_err(KERN_ERR "invalid cmd %d", cmd);
		ret = -EFAULT;
		break;
	}
	return ret;
}

/**
 * @brief This function will be called when the user read from sysfs.
 *
 * @param [in] dev device.
 * @param [in] attr device attributes.
 * @param [in/out] buf buffer.
 *
 * @return On success Returns maxlen characters pointed to by buf.
 *         On failure Returns negative error number.
 */
static ssize_t eep_get_size(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	eep_log(KERN_INFO "[eep_get_size] called.\n");

	mutex_lock(&eep_mutex);
	snprintf(buf, sizeof(int), "%d", eeprom_size);
	mutex_unlock(&eep_mutex);
	return strnlen(buf, MAX_ATTR_BYTES);
}

/* It specifies the eeprom address*/
static unsigned short normal_i2c[] = {
	EEPROM_SLAVE_ADDR, I2C_CLIENT_END
};

/**
 * @brief This function probes the eeprom chip.
 *
 * @param [in] client i2c client for eeprom.
 * @param [in] id structure address of eep_i2c_id.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
static int eep_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = 0;
	eep_log(KERN_INFO "[eep_probe] Called.\n");

	eep_client = client;
	/* lock write protection */
	#ifdef CONFIG_ARCH_SDP1406
		ret = eep_set_wp(0);
	#else
		ret = eep_set_wp(1);
	#endif
	return ret;
}

/**
 * @brief This function undo the all probing operations.
 *
 * @param [in] client i2c client for eeproom.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
static int eep_remove(struct i2c_client *client)
{
	int ret = 0;

	eep_log(KERN_INFO "[eep_remove] Called.\n");

	/* lock write protection */
	#ifdef CONFIG_ARCH_SDP1406
		ret = eep_set_wp(0);
	#else
		ret = eep_set_wp(1);
	#endif
	return ret;
}

/* for using sysfs. (class, device, device attribute) */
static struct class *eep_class;
static struct device *eep_dev;
static DEVICE_ATTR(nvram_size, S_IRUGO, eep_get_size, NULL);

static const struct of_device_id eepdev_of_match[] = {
	{ .compatible = "sii,s24c512c" },
	{},
};
MODULE_DEVICE_TABLE(of, eepdev_of_match);

/**
 * Devices are identified using device id
 * of the chip
 */
static const struct i2c_device_id eep_i2c_id[] = {
	{ "s24c512c", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, eep_i2c_id);

/**
 * During initialization, it registers a probe() method, which the I2C core
 * invokes when an associated host controller is detected.
 */
static struct i2c_driver eep_i2c_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
	.owner = THIS_MODULE,
	.name = "s24c512c",
	.of_match_table = of_match_ptr(eepdev_of_match),
	},
	.id_table = eep_i2c_id,
	.probe = eep_probe,
	.remove = eep_remove,
	.address_list = normal_i2c,
};

/* device number */
dev_t eeprom_dev_num;

/* function pointers for file operations */
static const struct file_operations eep_drv_fops = {
	.llseek = eep_lseek,
	.read = eep_read,
	.open = eep_open,
	.release = eep_close,
	.write = eep_write,
	.unlocked_ioctl = eep_ioctl,
	.owner = THIS_MODULE,
};

/* Device initialization routine */
static int __init eep_init(void)
{
	int res = 0;

	eep_log(KERN_INFO "[eep_init] init!!\n");

	counter = 0;
	eeprom_dev_major = 0;
	eeprom_dev_num = 0;
	cache_buf = NULL;
	eep_dev = NULL;
	eep_class = NULL;
	eep_client = NULL;

	/* register device with file operation mappings
	 * for dynamic allocation of major number
	 */
	res = register_chrdev(eeprom_dev_major, EEPROM_DEV_NAME, &eep_drv_fops);
	if (res < 0) {
		eep_err(KERN_CRIT "[eep_init] failed to get major number.\n");
		goto out;
	}
	/* if the allocation is dynamic ?*/
	if (res != 0)
		eeprom_dev_major = res;

	eeprom_dev_num = MKDEV(eeprom_dev_major, 0);

	/* create class. (/sys/class/eep_class) */
	eep_class = class_create(THIS_MODULE, "eep_class");
	if (IS_ERR(eep_class)) {
		res = PTR_ERR(eep_class);
		goto out_unreg_chrdev;
	}
	/* create class device. (/sys/class/eep_class/eeprom) */
	eep_dev = device_create(eep_class, NULL, eeprom_dev_num, NULL,
								"eeprom");

	if (IS_ERR(eep_dev)) {
		res = PTR_ERR(eep_dev);
		goto out_unreg_class;
	}
	/* create sysfs file. (/sys/class/eep_class/eeprom/nvram_size) */
	res = device_create_file(eep_dev, &dev_attr_nvram_size);
	if (res) {
		eep_err(KERN_CRIT "[eep_init] failed to create sysfs.\n");
		goto out_unreg_device;
	}
	res = i2c_add_driver(&eep_i2c_driver);
	if (res) {
		eep_err(KERN_CRIT "[eep_init] failed to add i2c driver.\n");
		goto out_unreg_sysfs;
	}
	/* memory allocation */
	cache_buf = vmalloc(eeprom_size);
	if (!cache_buf)
		eep_log(KERN_INFO "memory not allocated for cache buffer.\n");
	return res;

out_unreg_sysfs:
	device_remove_file(eep_dev, &dev_attr_nvram_size);
out_unreg_device:
	device_destroy(eep_class, eeprom_dev_num);
out_unreg_class:
	class_destroy(eep_class);
out_unreg_chrdev:
	unregister_chrdev(eeprom_dev_major, EEPROM_DEV_NAME);
out:
	return res;
}

/* Device exit routine */
static void __exit eep_exit(void)
{
	eep_log(KERN_INFO "[eep_exit] Exit!!\n");

	vfree(cache_buf);
	/* remove sysfs file */
	device_remove_file(eep_dev, &dev_attr_nvram_size);
	/* remove class device */
	device_destroy(eep_class, eeprom_dev_num);
	/* remove class */
	class_destroy(eep_class);
	i2c_del_driver(&eep_i2c_driver);
	/* unregister device */
	unregister_chrdev(eeprom_dev_major, EEPROM_DEV_NAME);
}

/* define module init/exit, license */
subsys_initcall(eep_init);
module_exit(eep_exit);

MODULE_AUTHOR("Dronamraju Santosh Pavan Kumar, <dronamraj.k@samsung.com>");
MODULE_DESCRIPTION("s24c512c EEPROM driver");
MODULE_LICENSE("GPL");

