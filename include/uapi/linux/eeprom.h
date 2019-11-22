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
 * @file eeprom.h
 * @brief Header file for the eeprom_s24c512c eeprom chip.
 * @author Dronamraju Santosh pavan Kumar <dronamraj.k@samsung.com>
 * @date   2013/07/12
 *
 */

/* internal Release1 */

#ifndef	_EEPROM_H
#define	_EEPROM_H

#include<asm-generic/ioctl.h>

/*To enable debug logs,uncomment below EEP_DEBUG macro*/
/*#define EEP_DEBUG*/
#ifdef EEP_DEBUG
#define eep_log(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define eep_log(fmt, ...)
#endif

/*Below macro is for eep_err, it is always printed */
#define eep_err(fmt, ...) printk(fmt, ##__VA_ARGS__)

#define EEPROM_MAGIC 'I'
#define EEPROM_LOCAL_BUFF_SIZE 128

/*device name*/
#define EEPROM_DEV_NAME			"eeprom"

struct eeprom_io_pkt {
	unsigned int addr;
	unsigned int size;
	const char *wbuf;
	char *rbuf;
};

#define	EEPROM_RESET			_IOW(EEPROM_MAGIC, 0x01, int)
#define	EEPROM_SET_WP			_IOW(EEPROM_MAGIC, 0x02, int)
#define	EEPROM_GET_WP			_IOR(EEPROM_MAGIC, 0x03, int)
#define	EEPROM_GET_SIZE			_IOR(EEPROM_MAGIC, 0x04, int)
#define EEPROM_WRITE_DATA		_IOW(EEPROM_MAGIC, 0x05, struct eeprom_io_pkt *)
#define EEPROM_READ_DATA		_IOR(EEPROM_MAGIC, 0x06, struct eeprom_io_pkt *)
#define	EEPROM_SLAVE_ADDR		0x50
#define	EEPROM_MAX_CMDS			6

/* kernel function prototypes */
#ifdef __KERNEL__
int eep_i2c_write(const unsigned char *buf, int len);
int eep_reg_write(unsigned int reg, const unsigned char *buf, int len);
int eep_reg_read(unsigned int reg, unsigned char *buf, int len);
int eep_set_wp(int protect);
int eep_get_wp(void);
ssize_t eeprom_dev_write(char *buf, size_t count, unsigned long addr);
ssize_t eeprom_dev_read(char *buf, size_t count, unsigned long addr);
#endif

#endif	/*_EEPROM_H */
