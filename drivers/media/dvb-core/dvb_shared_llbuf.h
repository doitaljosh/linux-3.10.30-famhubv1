/*  drivers/media/dvb-core/dvb_llbuffer.h
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
#ifndef _DVB_LLBUFFER_H_
#define _DVB_LLBUFFER_H_

#include <linux/spinlock.h>
#include <linux/wait.h>
#include "dvb_ringbuffer.h"

#define DVB_LLBUF_BLOCK_SIZE    (1024 * 4)

/* initialize linked list buffer, lock and queue */
extern void dvb_llbuf_init(struct dvb_ringbuffer *rbuf, void *data, size_t len);

/* test whether buffer is empty */
extern int dvb_llbuf_empty(struct dvb_ringbuffer *rbuf);

/* return the number of free bytes in the buffer */
extern ssize_t dvb_llbuf_free(struct dvb_ringbuffer *rbuf);

/* return the number of bytes waiting in the buffer */
extern ssize_t dvb_llbuf_avail(struct dvb_ringbuffer *rbuf);


/*
** Reset the read and write pointers to zero and flush the buffer
** This counts as a read and write operation
*/
extern void dvb_llbuf_reset(struct dvb_ringbuffer *rbuf);

extern void dvb_llbuf_flush(struct dvb_ringbuffer *rbuf);

/*
** read <len> bytes from linked list buffer into <buf>
** <usermem> specifies whether <buf> resides in user space
** returns number of bytes transferred or -EFAULT
*/
extern ssize_t dvb_llbuf_read_user(struct dvb_ringbuffer *rbuf,
				   u8 __user *buf, size_t len);
extern void dvb_llbuf_read(struct dvb_ringbuffer *rbuf,
				   u8 *buf, size_t len);


/* write routines & macros */
/* ----------------------- */
/*
** write <len> bytes to ring buffer
** <usermem> specifies whether <buf> resides in user space
** returns number of bytes transferred or -EFAULT
*/
extern ssize_t dvb_llbuf_write(struct dvb_ringbuffer *rbuf, const u8 *buf,
				    size_t len);

#endif /* _DVB_LLBUFFER_H_ */
