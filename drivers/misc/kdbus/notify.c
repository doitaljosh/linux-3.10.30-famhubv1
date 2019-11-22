/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "bus.h"
#include "connection.h"
#include "message.h"
#include "notify.h"

static int kdbus_notify_reply(struct kdbus_bus *bus, u64 id,
			      u64 cookie, u64 msg_type)
{
	struct kdbus_kmsg *kmsg = NULL;
	int ret;

	BUG_ON(id == 0);

	ret = kdbus_kmsg_new(0, &kmsg);
	if (ret < 0)
		return ret;

	/*
	 * a kernel-generated notification can only contain one
	 * struct kdbus_item, so make a shortcut here for
	 * faster lookup in the match db.
	 */
	kmsg->notify_type = msg_type;
	kmsg->msg.dst_id = id;
	kmsg->msg.src_id = KDBUS_SRC_ID_KERNEL;
	kmsg->msg.payload_type = KDBUS_PAYLOAD_KERNEL;
	kmsg->msg.cookie_reply = cookie;
	kmsg->msg.items[0].type = msg_type;

	spin_lock(&bus->notify_lock);
	list_add_tail(&kmsg->queue_entry, &bus->notify_list);
	spin_unlock(&bus->notify_lock);
	return ret;
}

/**
 * kdbus_notify_reply_timeout() - queue a timeout reply
 * @bus:		Bus which queues the messages
 * @id:			The destination's connection ID
 * @cookie:		The cookie to set in the reply.
 *
 * Queues a message that has a KDBUS_ITEM_REPLY_TIMEOUT item attached.
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_notify_reply_timeout(struct kdbus_bus *bus, u64 id, u64 cookie)
{
	return kdbus_notify_reply(bus, id, cookie, KDBUS_ITEM_REPLY_TIMEOUT);
}

/**
 * kdbus_notify_reply_dead() - queue a 'dead' reply
 * @bus:		Bus which queues the messages
 * @id:			The destination's connection ID
 * @cookie:		The cookie to set in the reply.
 *
 * Queues a message that has a KDBUS_ITEM_REPLY_DEAD item attached.
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_notify_reply_dead(struct kdbus_bus *bus, u64 id, u64 cookie)
{
	return kdbus_notify_reply(bus, id, cookie, KDBUS_ITEM_REPLY_DEAD);
}

/**
 * kdbus_notify_name_change() - queue a notification about a name owner change
 * @bus:		Bus which queues the messages
 * @type:		The type if the notification; KDBUS_ITEM_NAME_ADD,
 *			KDBUS_ITEM_NAME_CHANGE or KDBUS_ITEM_NAME_REMOVE
 * @old_id:		The id of the connection that used to own the name
 * @new_id:		The id of the new owner connection
 * @old_flags:		The flags to pass in the KDBUS_ITEM flags field for
 *			the old owner
 * @new_flags:		The flags to pass in the KDBUS_ITEM flags field for
 *			the new owner
 * @name:		The name that was removed or assigned to a new owner
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_notify_name_change(struct kdbus_bus *bus, u64 type,
			     u64 old_id, u64 new_id,
			     u64 old_flags, u64 new_flags,
			     const char *name)
{
	struct kdbus_kmsg *kmsg = NULL;
	size_t name_len, extra_size;
	int ret;

	name_len = strlen(name) + 1;
	extra_size = sizeof(struct kdbus_notify_name_change) + name_len;
	ret = kdbus_kmsg_new(extra_size, &kmsg);
	if (ret < 0)
		return ret;

	kmsg->msg.dst_id = KDBUS_DST_ID_BROADCAST;
	kmsg->msg.src_id = KDBUS_SRC_ID_KERNEL;
	kmsg->notify_type = type;
	kmsg->notify_old_id = old_id;
	kmsg->notify_new_id = new_id;
	kmsg->msg.items[0].type = type;
	kmsg->msg.items[0].name_change.old.id = old_id;
	kmsg->msg.items[0].name_change.old.flags = old_flags;
	kmsg->msg.items[0].name_change.new.id = new_id;
	kmsg->msg.items[0].name_change.new.flags = new_flags;
	memcpy(kmsg->msg.items[0].name_change.name, name, name_len);
	kmsg->notify_name = kmsg->msg.items[0].name_change.name;

	spin_lock(&bus->notify_lock);
	list_add_tail(&kmsg->queue_entry, &bus->notify_list);
	spin_unlock(&bus->notify_lock);
	return ret;
}

/**
 * kdbus_notify_id_change() - queue a notification about a unique ID change
 * @bus:		Bus which queues the messages
 * @type:		The type if the notification; KDBUS_ITEM_ID_ADD or
 *			KDBUS_ITEM_ID_REMOVE
 * @id:			The id of the connection that was added or removed
 * @flags:		The flags to pass in the KDBUS_ITEM flags field
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_notify_id_change(struct kdbus_bus *bus, u64 type, u64 id, u64 flags)
{
	struct kdbus_kmsg *kmsg = NULL;
	int ret;

	ret = kdbus_kmsg_new(sizeof(struct kdbus_notify_id_change), &kmsg);
	if (ret < 0)
		return ret;

	kmsg->msg.dst_id = KDBUS_DST_ID_BROADCAST;
	kmsg->msg.src_id = KDBUS_SRC_ID_KERNEL;
	kmsg->notify_type = type;

	switch (type) {
	case KDBUS_ITEM_ID_ADD:
		kmsg->notify_new_id = id;
		break;

	case KDBUS_ITEM_ID_REMOVE:
		kmsg->notify_old_id = id;
		break;

	default:
		BUG();
	}

	kmsg->msg.items[0].type = type;
	kmsg->msg.items[0].id_change.id = id;
	kmsg->msg.items[0].id_change.flags = flags;

	spin_lock(&bus->notify_lock);
	list_add_tail(&kmsg->queue_entry, &bus->notify_list);
	spin_unlock(&bus->notify_lock);
	return ret;
}

/**
 * kdbus_notify_flush() - send a list of collected messages
 * @bus:		Bus which queues the messages
 *
 * The list is empty after sending the messages.
 */
void kdbus_notify_flush(struct kdbus_bus *bus)
{
	LIST_HEAD(notify_list);
	struct kdbus_kmsg *kmsg, *tmp;

	mutex_lock(&bus->notify_flush_lock);

	spin_lock(&bus->notify_lock);
	list_splice_init(&bus->notify_list, &notify_list);
	spin_unlock(&bus->notify_lock);

	list_for_each_entry_safe(kmsg, tmp, &notify_list, queue_entry) {
		kdbus_conn_kmsg_send(bus->ep, NULL, kmsg);
		list_del(&kmsg->queue_entry);
		kdbus_kmsg_free(kmsg);
	}

	mutex_unlock(&bus->notify_flush_lock);
}

/**
 * kdbus_notify_free() - free a list of collected messages
 * @bus:		Bus which queues the messages
 */
void kdbus_notify_free(struct kdbus_bus *bus)
{
	struct kdbus_kmsg *kmsg, *tmp;

	list_for_each_entry_safe(kmsg, tmp, &bus->notify_list, queue_entry) {
		list_del(&kmsg->queue_entry);
		kdbus_kmsg_free(kmsg);
	}
}
