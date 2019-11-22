/*  drivers/media/dvb-core/linked-rbuf.c
 *
 *  Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "dvb_llbuffer.h"

#define DVB_LLBUF_NUM_OF_BLOCKS (400)

struct dvb_buf_node {
	struct list_head	p_list;	/* link up with pool_list */
	struct list_head	b_list;	/* link up with buf list */
	u8	*data;
	bool	is_allocated;	/* whether buf node is allocated in buf_list */
	bool	is_wbusy;	/* write busy flag */
	bool	is_rbusy;	/* read busy flag */
	ssize_t	pwrite;		/* write position */
	ssize_t	pread;		/* read position */
};

struct dvb_buf_pool {
	struct list_head	pool_list;	/* list head */
	spinlock_t	lock;
	atomic_t	free_num; /* the number of free buf_node in the pool */
} buf_pool;

/* alloc new buf_node */
void dvb_llbuf_alloc_buf_node_in_pool(struct dvb_llbuffer *rbuf)
{
	struct dvb_buf_node *buf_node;
	struct list_head *pos, *next;

	list_for_each_safe(pos, next, &buf_pool.pool_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, p_list);

		if (unlikely(buf_node->is_allocated)) {
			if (buf_node->is_rbusy || buf_node->is_wbusy)
				continue;
			pr_warn("%s:DATA WILL BE DELETED!\n", __func__);
			buf_node->pread = 0;
			buf_node->pwrite = 0;
			list_del(&buf_node->b_list);
		}
		buf_node->is_allocated = true;
		list_add_tail(&buf_node->b_list, &rbuf->buf_list);
		list_move_tail(&buf_node->p_list, &buf_pool.pool_list);
		atomic_dec(&buf_pool.free_num);
		return;
	}
}

/* initialize linked list buffer, lock and queue */
void dvb_llbuf_init(struct dvb_llbuffer *rbuf, void *data, size_t len)
{
	rbuf->pread = rbuf->pwrite = 0;
	rbuf->data = data;
	rbuf->size = len;
	rbuf->error = 0;

	if (rbuf->is_initialized == 1) {
		dvb_llbuf_reset(rbuf);
		return;
	}

	INIT_LIST_HEAD(&rbuf->buf_list);

	rbuf->is_initialized = 1;

	init_waitqueue_head(&rbuf->queue);
	spin_lock_init(&(rbuf->lock));
}
EXPORT_SYMBOL(dvb_llbuf_init);

/* test whether buffer is empty */
int dvb_llbuf_empty(struct dvb_llbuffer *rbuf)
{
	struct list_head *pos, *next;
	struct dvb_buf_node *buf_node;
	unsigned long flags;

	spin_lock_irqsave(&buf_pool.lock, flags);
	list_for_each_safe(pos, next, &rbuf->buf_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, b_list);
		if (buf_node->pwrite != buf_node->pread) {
			spin_unlock_irqrestore(&buf_pool.lock, flags);
			return false;
		}
	}
	spin_unlock_irqrestore(&buf_pool.lock, flags);
	return true;
}
EXPORT_SYMBOL(dvb_llbuf_empty);

/* return the number of free bytes in the buffer */
ssize_t dvb_llbuf_free(struct dvb_llbuffer *rbuf)
{
	return atomic_read(&buf_pool.free_num) * DVB_LLBUF_BLOCK_SIZE;
}
EXPORT_SYMBOL(dvb_llbuf_free);

/* return the number of bytes waiting in the buffer */
ssize_t dvb_llbuf_avail(struct dvb_llbuffer *rbuf)
{
	struct list_head *pos, *next;
	struct dvb_buf_node *buf_node;
	unsigned long flags;
	ssize_t avail = 0;

	spin_lock_irqsave(&buf_pool.lock, flags);
	list_for_each_safe(pos, next, &rbuf->buf_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, b_list);
		avail += buf_node->pwrite - buf_node->pread;
	}
	spin_unlock_irqrestore(&buf_pool.lock, flags);
	return avail;
}
EXPORT_SYMBOL(dvb_llbuf_avail);

/* reset the linked list buffer */
void dvb_llbuf_reset(struct dvb_llbuffer *rbuf)
{
	struct list_head *pos, *next;
	struct dvb_buf_node *buf_node;
	unsigned long flags;

	spin_lock_irqsave(&buf_pool.lock, flags);
	list_for_each_safe(pos, next, &rbuf->buf_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, b_list);
		buf_node->is_allocated = false;
		buf_node->pread = 0;
		buf_node->pwrite = 0;
		list_move(&buf_node->p_list, &buf_pool.pool_list);
		list_del(&buf_node->b_list);
		atomic_inc(&buf_pool.free_num);
	}
	rbuf->error = 0;
	spin_unlock_irqrestore(&buf_pool.lock, flags);
}

ssize_t dvb_llbuf_read_user(struct dvb_llbuffer *rbuf,
				   u8 __user *buf, size_t len)
{
	struct dvb_buf_node *buf_node;
	ssize_t avail = 0;
	ssize_t todo = len;
	unsigned long flags;

	while (todo > 0) {
		spin_lock_irqsave(&buf_pool.lock, flags);
		/* get the number of bytes waiting in the buf_node */
		buf_node = list_first_entry_or_null(&rbuf->buf_list,
				struct dvb_buf_node, b_list);
		if (!buf_node)
			break;
		avail =  buf_node->pwrite - buf_node->pread;
		if (avail) {
			/* copy the buffer */
			avail = (avail > todo) ? todo : avail;
			memcpy(rbuf->data, buf_node->data + buf_node->pread,
				avail);
			buf_node->pread += avail;
			todo -= avail;
			/* if all data is read, delete buf_node */
			if (buf_node->pread == buf_node->pwrite) {
				buf_node->is_allocated = false;
				buf_node->pread = 0;
				buf_node->pwrite = 0;
				list_move(&buf_node->p_list,
					&buf_pool.pool_list);
				list_del(&buf_node->b_list);
				atomic_inc(&buf_pool.free_num);
			}
			buf_node->is_rbusy = false;
		}
		spin_unlock_irqrestore(&buf_pool.lock, flags);

		if (copy_to_user(buf, rbuf->data, avail))
			return -EFAULT;
		buf += avail;

	}
	return len - todo;
}
EXPORT_SYMBOL(dvb_llbuf_read_user);

void dvb_llbuf_read(struct dvb_llbuffer *rbuf,
				   u8 *buf, size_t len)
{
	struct dvb_buf_node *buf_node;
	ssize_t avail = 0;
	ssize_t todo = len;
	unsigned long flags;

	while (todo > 0) {
		spin_lock_irqsave(&buf_pool.lock, flags);
		/* get the number of bytes waiting in the buf_node */
		buf_node = list_first_entry_or_null(&rbuf->buf_list,
				struct dvb_buf_node, b_list);
		if (!buf_node)
			break;
		avail =  buf_node->pwrite - buf_node->pread;
		if (avail) {
			/* copy the buffer */
			avail = (avail > todo) ? todo : avail;
			memcpy(buf, buf_node->data + buf_node->pread, avail);
			buf_node->pread += avail;
			buf += avail;
			todo -= avail;
			/* if all data is read, delete buf_node */
			if (buf_node->pread == buf_node->pwrite) {
				buf_node->is_allocated = false;
				buf_node->pread = 0;
				buf_node->pwrite = 0;
				list_move(&buf_node->p_list,
					&buf_pool.pool_list);
				list_del(&buf_node->b_list);
				atomic_inc(&buf_pool.free_num);
			}
			buf_node->is_rbusy = false;
		}
		spin_unlock_irqrestore(&buf_pool.lock, flags);
	}
}
EXPORT_SYMBOL(dvb_llbuf_read);

ssize_t dvb_llbuf_write(struct dvb_llbuffer *rbuf, const u8 *buf,
				    size_t len)
{
	struct dvb_buf_node *buf_node = NULL;
	ssize_t free = 0;
	ssize_t todo = len;
	unsigned long flags;

	while (todo > 0) {
		spin_lock_irqsave(&buf_pool.lock, flags);
		/* get the number of free bytes in the last buf_node */
		if (!list_empty(&rbuf->buf_list)) {
			buf_node = list_entry((&rbuf->buf_list)->prev,
				struct dvb_buf_node, b_list);
			free = DVB_LLBUF_BLOCK_SIZE - buf_node->pwrite;
		}
		/* if the free buffer is insufficient,
		 * alloc new buf_node in the pool
		 */
		if (!free) {
			dvb_llbuf_alloc_buf_node_in_pool(rbuf);
			free = DVB_LLBUF_BLOCK_SIZE;
			buf_node = list_entry((&rbuf->buf_list)->prev,
				struct dvb_buf_node, b_list);
		}
		/* copy the buffer */
		buf_node->is_wbusy = true;
		free = (free > todo) ? todo : free;
		memcpy(buf_node->data + buf_node->pwrite, buf, free);
		buf_node->pwrite += free;
		todo -= free;
		buf += free;
		free = 0;
		buf_node->is_wbusy = false;
		spin_unlock_irqrestore(&buf_pool.lock, flags);
	}
	return len;
}
EXPORT_SYMBOL(dvb_llbuf_write);

/* flush the linked list buffer */
void dvb_llbuf_flush(struct dvb_llbuffer *rbuf)
{
	struct list_head *pos, *next;
	struct dvb_buf_node *buf_node;
	unsigned long flags;

	spin_lock_irqsave(&buf_pool.lock, flags);
	list_for_each_safe(pos, next, &rbuf->buf_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, b_list);
		buf_node->is_allocated = false;
		buf_node->pread = 0;
		buf_node->pwrite = 0;
		list_move(&buf_node->p_list, &buf_pool.pool_list);
		list_del(&buf_node->b_list);
		atomic_inc(&buf_pool.free_num);
	}
	rbuf->error = 0;
	spin_unlock_irqrestore(&buf_pool.lock, flags);
}
EXPORT_SYMBOL(dvb_llbuf_flush);

static int __init dvb_buf_init(void)
{
	struct dvb_buf_node *buf_node;
	int i;

	INIT_LIST_HEAD(&buf_pool.pool_list);
	spin_lock_init(&buf_pool.lock);
	atomic_set(&buf_pool.free_num, DVB_LLBUF_NUM_OF_BLOCKS);

	for (i = 0; i < DVB_LLBUF_NUM_OF_BLOCKS; i++) {
		buf_node = vmalloc(sizeof(struct dvb_buf_node));
		if (!buf_node) {
			pr_err("%s: vmalloc failed\n", __func__);
			return -ENOMEM;
		}
		buf_node->data = vmalloc(DVB_LLBUF_BLOCK_SIZE);
		if (!buf_node->data) {
			pr_err("%s: vmalloc failed\n", __func__);
			return -ENOMEM;
		}
		memset(buf_node->data, 0, DVB_LLBUF_BLOCK_SIZE);
		buf_node->is_allocated = false;
		buf_node->is_wbusy = false;
		buf_node->is_rbusy = false;
		buf_node->pread = 0;
		buf_node->pwrite = 0;
		list_add_tail(&buf_node->p_list, &buf_pool.pool_list);
	}
	return 0;
}
module_init(dvb_buf_init);

static void __exit dvb_buf_exit(void)
{
	struct list_head *pos, *next;
	struct dvb_buf_node *buf_node;
	unsigned long flags;

	spin_lock_irqsave(&buf_pool.lock, flags);
	list_for_each_safe(pos, next, &buf_pool.pool_list) {
		buf_node = list_entry(pos, struct dvb_buf_node, p_list);
		list_del(&buf_node->b_list);
		list_del(&buf_node->p_list);
		vfree(buf_node->data);
		vfree(buf_node);
	}
	spin_unlock_irqrestore(&buf_pool.lock, flags);
}
module_exit(dvb_buf_exit);

MODULE_AUTHOR("ms17.kim@samsung.com");
MODULE_DESCRIPTION("Samsung Electronics Corp linked list ring buffer driver");
MODULE_LICENSE("GPL");

