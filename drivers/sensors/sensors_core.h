/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _SENSORS_CORE_H_
#define _SENSORS_CORE_H_

extern int sensors_create_symlink(struct kobject *, const char *);
extern void sensors_remove_symlink(struct kobject *, const char *);
extern int sensors_register(struct device *, void *,
		struct device_attribute *[], char *);
extern void sensors_unregister(struct device *, struct device_attribute *[]);
extern void destroy_sensor_class(void);
extern void remap_sensor_data(s16 *, int);

#endif
