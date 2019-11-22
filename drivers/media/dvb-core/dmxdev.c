/*
 * dmxdev.c - DVB demultiplexer device
 *
 * Copyright (C) 2000 Ralph Metzler & Marcus Metzler
 *		      for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include "dmxdev.h"

#include <t2ddebugd/t2ddebugd.h>
#include <linux/t2d_print.h>

#include <linux/of_gpio.h>

static int syscall_debug = 0;
static int pes_write_debug = -1;
static int pes_read_debug = -1;
static int section_write_debug = -1;
static int section_read_debug = -1;
static int rb_debug = 0;
static int dvr_debug = 0;
#define scdbg_printk(args...) \
	do { \
		if (syscall_debug) { \
			printk(args); \
		} \
	} while (0)

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk	if (debug) printk

#define DEFAULT_DMX_BUFFER_SIZE	(128*1024)

#ifdef CONFIG_DVB_LINKED_RBUF
static int dvb_dmxdev_buffer_write(struct dvb_llbuffer *buf,
				   const u8 *src, size_t len)
#else
static int dvb_dmxdev_buffer_write(struct dvb_ringbuffer *buf,
				   const u8 *src, size_t len)
#endif
{
	ssize_t free;

	if (!len)
		return 0;
	if (!buf->data)
		return 0;

#ifdef CONFIG_DVB_LINKED_RBUF
	free = dvb_llbuf_free(buf);
#else
	free = dvb_ringbuffer_free(buf);
#endif
	if (len > (size_t)free) {
		dprintk("dmxdev: buffer overflow\n");
		if (syscall_debug)
			pr_err("dmxdev: buffer overflow\n");
		return -EOVERFLOW;
	}

#ifdef CONFIG_DVB_LINKED_RBUF
	return dvb_llbuf_write(buf, src, len);
#else
	return dvb_ringbuffer_write(buf, src, len);
#endif
}

#ifdef CONFIG_DVB_LINKED_RBUF
static ssize_t dvb_dmxdev_buffer_read(struct dvb_llbuffer *src,
				      int non_blocking, char __user *buf,
				      size_t count, loff_t *ppos)
#else
static ssize_t dvb_dmxdev_buffer_read(struct dvb_ringbuffer *src,
				      int non_blocking, char __user *buf,
				      size_t count, loff_t *ppos)
#endif
{
	size_t todo;
	ssize_t avail;
	ssize_t ret = 0;

	if (!src->data)
		return 0;

	if (src->error) {
		ret = src->error;
#ifdef CONFIG_DVB_LINKED_RBUF
		dvb_llbuf_flush(src);
#else
		dvb_ringbuffer_flush(src);
#endif
		return ret;
	}

	for (todo = count; todo > 0; todo -= (size_t)ret) {
#ifdef CONFIG_DVB_LINKED_RBUF
		if (non_blocking && dvb_llbuf_empty(src)) {
#else
		if (non_blocking && dvb_ringbuffer_empty(src)) {
#endif
			ret = -EWOULDBLOCK;
			break;
		}

#ifdef CONFIG_DVB_LINKED_RBUF
		ret = wait_event_interruptible(src->queue,
					       !dvb_llbuf_empty(src) ||
					       (src->error != 0));
#else
		ret = wait_event_interruptible(src->queue,
					       !dvb_ringbuffer_empty(src) ||
					       (src->error != 0));
#endif
		if (ret < 0)
			break;

		if (src->error) {
			ret = src->error;
#ifdef CONFIG_DVB_LINKED_RBUF
			dvb_llbuf_flush(src);
#else
			dvb_ringbuffer_flush(src);
#endif
			break;
		}

#ifdef CONFIG_DVB_LINKED_RBUF
		avail = dvb_llbuf_avail(src);
#else
		avail = dvb_ringbuffer_avail(src);
#endif
		if (avail > (ssize_t)todo)
			avail = (ssize_t)todo;

#ifdef CONFIG_DVB_LINKED_RBUF
		ret = dvb_llbuf_read_user(src, buf, (size_t)avail);
#else
		ret = dvb_ringbuffer_read_user(src, buf, (size_t)avail);
#endif
		if (ret < 0)
			break;

		buf += ret;
	}

	return (count - todo) ? (ssize_t)(count - todo) : ret;
}

static struct dmx_frontend *get_fe(struct dmx_demux *demux, int type)
{
	struct list_head *head, *pos;

	head = demux->get_frontends(demux);
	if (!head)
		return NULL;
	list_for_each(pos, head)
		if (DMX_FE_ENTRY(pos)->source == (size_t)type) 
			return DMX_FE_ENTRY(pos);

	return NULL;
}

static int dvb_dvr_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	struct dmx_frontend *front;
	struct dmx_demux *demux = dmxdev->demux;
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;
#ifdef CONFIG_DVB_LINKED_RBUF
	size_t size = DVB_LLBUF_BLOCK_SIZE;
#else
	size_t size = DVR_BUFFER_SIZE;
#endif

	dprintk("function : %s\n", __func__);

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	if (dmxdev->exit) {
		mutex_unlock(&dmxdev->mutex);
		return -ENODEV;
	}

	if ((file->f_flags & O_ACCMODE) == O_RDWR) {
		if (!(dmxdev->capabilities & DMXDEV_CAP_DUPLEX)) {
			mutex_unlock(&dmxdev->mutex);
			return -EOPNOTSUPP;
		}
	}

	// Clear flag, Playback
	dvbdemux->record_mode = DMX_REC_MODE_DIGITAL;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		void *mem;
		if (!dvbdev->readers) {
			mutex_unlock(&dmxdev->mutex);
			return -EBUSY;
		}
		mem = vmalloc((unsigned long)size);
		if (!mem) {
			mutex_unlock(&dmxdev->mutex);
			return -ENOMEM;
		}
		// DVR record is implemented by hardware. So need to check whether we can use such hardware DVR recorder.
		if (dmxdev->capabilities & DMXDEV_CAP_SCRAMBLE_DVR) {
			if (!dmxdev->scr_dvr->open) {
				vfree(mem);
				mutex_unlock(&dmxdev->mutex);
				return -EOPNOTSUPP;
			}

			// Open DVR recorder fail
			if (dmxdev->scr_dvr->open(dmxdev->scr_dvr)) {
				vfree(mem);
				mutex_unlock(&dmxdev->mutex);
				return -EBUSY;
			}
		}

		if (dvr_debug) {
			pr_err("dvr_open: file=%p, buffer=%p\n", file, &dmxdev->dvr_buffer);
		}
#ifdef CONFIG_DVB_LINKED_RBUF
		dvb_llbuf_init(&dmxdev->dvr_buffer, mem, size);
#else
		dvb_ringbuffer_init(&dmxdev->dvr_buffer, mem, size);
#endif
		dvbdev->readers--;
	}

	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		dmxdev->dvr_orig_fe = dmxdev->demux->frontend;

		if (!dmxdev->demux->write) {
			mutex_unlock(&dmxdev->mutex);
			return -EOPNOTSUPP;
		}

		front = get_fe(dmxdev->demux, DMX_MEMORY_FE);

		if (!front) {
			mutex_unlock(&dmxdev->mutex);
			return -EINVAL;
		}
		dmxdev->demux->disconnect_frontend(dmxdev->demux);
		dmxdev->demux->connect_frontend(dmxdev->demux, front);
	}
	dvbdev->users++;
	mutex_unlock(&dmxdev->mutex);
	return 0;
}

static int dvb_dvr_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	struct dmx_demux *demux = dmxdev->demux;
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	mutex_lock(&dmxdev->mutex);

	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		dmxdev->demux->disconnect_frontend(dmxdev->demux);
		dmxdev->demux->connect_frontend(dmxdev->demux,
						dmxdev->dvr_orig_fe);
	}
	if (dvr_debug) {
		pr_err("dvr_release: file=%p, buffer=%p\n", file, &dmxdev->dvr_buffer);
	}
	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		dvbdev->readers++;
		if (dmxdev->dvr_buffer.data) {
			void *mem = dmxdev->dvr_buffer.data;
			mb();
			spin_lock_irq(&dmxdev->lock);
			dmxdev->dvr_buffer.data = NULL;
			spin_unlock_irq(&dmxdev->lock);
			vfree(mem);
#ifdef CONFIG_DVB_LINKED_RBUF
			dvb_llbuf_flush(&dmxdev->dvr_buffer);
#endif
		}
		// DVR record is implemented by hardware. So need to check whether we can use such hardware DVR recorder.
		if (dmxdev->capabilities & DMXDEV_CAP_SCRAMBLE_DVR) {
			if (dmxdev->scr_dvr->close) {
				dmxdev->scr_dvr->close(dmxdev->scr_dvr);
			}
		}
	}
	/* TODO */
	dvbdev->users--;
	if (dvbdev->users == 1 && dmxdev->exit == 1) {
		fops_put(file->f_op);
		file->f_op = NULL;
		mutex_unlock(&dmxdev->mutex);
		wake_up(&dvbdev->wait_queue);
	} else
		mutex_unlock(&dmxdev->mutex);

	// Clear flag, Playback
	dvbdemux->record_mode = DMX_REC_MODE_DIGITAL;

	return 0;
}

static ssize_t dvb_dvr_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	int ret;

	if (!dmxdev->demux->write)
		return -EOPNOTSUPP;
	if ((file->f_flags & O_ACCMODE) != O_WRONLY)
		return -EINVAL;
	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	if (dmxdev->exit) {
		mutex_unlock(&dmxdev->mutex);
		return -ENODEV;
	}
	if (dvr_debug) {
		pr_err("dvr_write: file=%p, count=%d\n", file, count);
	}
	ret = dmxdev->demux->write(dmxdev->demux, buf, count);
	mutex_unlock(&dmxdev->mutex);
	return ret;
}

static ssize_t dvb_dvr_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;

	if (dmxdev->exit)
		return -ENODEV;
	if (dvr_debug) {
		pr_err("dvr_read: file=%p, buffer=%p, count=%d\n", file, &dmxdev->dvr_buffer, count);
	}

	return dvb_dmxdev_buffer_read(&dmxdev->dvr_buffer,
				      file->f_flags & O_NONBLOCK,
				      buf, count, ppos);
}

static int dvb_dvr_set_buffer_size(struct dmxdev *dmxdev,
				      unsigned long size)
{
#ifdef CONFIG_DVB_LINKED_RBUF
	struct dvb_llbuffer *buf = &dmxdev->dvr_buffer;
#else
	struct dvb_ringbuffer *buf = &dmxdev->dvr_buffer;
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	void *newmem;
	void *oldmem;

	dprintk("function : %s\n", __func__);

	if (buf->size == (ssize_t)size)
		return 0;
	if (!size)
		return -EINVAL;

	newmem = vmalloc(size);
	if (!newmem)
		return -ENOMEM;

	oldmem = buf->data;

	spin_lock_irq(&dmxdev->lock);
	buf->data = newmem;
	buf->size = (ssize_t)size;

#endif
	/* reset and not flush in case the buffer shrinks */
#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_reset(buf);
#else
	dvb_ringbuffer_reset(buf);
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	spin_unlock_irq(&dmxdev->lock);

	vfree(oldmem);
#endif
	return 0;
}

static inline void dvb_dmxdev_filter_state_set(struct dmxdev_filter
					       *dmxdevfilter, int state)
{
	spin_lock_irq(&dmxdevfilter->dev->lock);
	dmxdevfilter->state = state;
	spin_unlock_irq(&dmxdevfilter->dev->lock);
}

static int dvb_dmxdev_set_buffer_size(struct dmxdev_filter *dmxdevfilter,
				      unsigned long size)
{
#ifdef CONFIG_DVB_LINKED_RBUF
	struct dvb_llbuffer *buf = &dmxdevfilter->buffer;
#else
	struct dvb_ringbuffer *buf = &dmxdevfilter->buffer;
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	void *newmem;
	void *oldmem;

	if (buf->size == (ssize_t)size)
		return 0;
	if (!size)
		return -EINVAL;
	if (dmxdevfilter->state >= DMXDEV_STATE_GO)
		return -EBUSY;

	newmem = vmalloc(size);
	if (!newmem)
		return -ENOMEM;

	oldmem = buf->data;

	spin_lock_irq(&dmxdevfilter->dev->lock);
	buf->data = newmem;
	buf->size = (ssize_t)size;

#endif
	/* reset and not flush in case the buffer shrinks */
#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_reset(buf);
#else
	dvb_ringbuffer_reset(buf);
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	spin_unlock_irq(&dmxdevfilter->dev->lock);

	vfree(oldmem);
#endif
	return 0;
}

static void dvb_dmxdev_filter_timeout(unsigned long data)
{
	struct dmxdev_filter *dmxdevfilter = (struct dmxdev_filter *)data;

	dmxdevfilter->buffer.error = -ETIMEDOUT;
	spin_lock_irq(&dmxdevfilter->dev->lock);
	dmxdevfilter->state = DMXDEV_STATE_TIMEDOUT;
	spin_unlock_irq(&dmxdevfilter->dev->lock);
	wake_up(&dmxdevfilter->buffer.queue);
}

static void dvb_dmxdev_filter_timer(struct dmxdev_filter *dmxdevfilter)
{
	struct dmx_sct_filter_params *para = &dmxdevfilter->params.sec;

	del_timer(&dmxdevfilter->timer);
	if (para->timeout) {
		dmxdevfilter->timer.function = dvb_dmxdev_filter_timeout;
		dmxdevfilter->timer.data = (unsigned long)dmxdevfilter;
		dmxdevfilter->timer.expires =
		    jiffies + 1 + (HZ / 2 + HZ * para->timeout) / 1000;
		add_timer(&dmxdevfilter->timer);
	}
}

static int dvb_dmxdev_section_callback(const u8 *buffer1, size_t buffer1_len,
				       const u8 *buffer2, size_t buffer2_len,
				       struct dmx_section_filter *filter,
				       enum dmx_success success)
{
	struct dmxdev_filter *dmxdevfilter = filter->priv;
	int ret;

	if (dmxdevfilter->buffer.error) {
		wake_up(&dmxdevfilter->buffer.queue);
		return 0;
	}
	spin_lock(&dmxdevfilter->dev->lock);
	if (dmxdevfilter->state != DMXDEV_STATE_GO) {
		spin_unlock(&dmxdevfilter->dev->lock);
		return 0;
	}
	del_timer(&dmxdevfilter->timer);
	dprintk("dmxdev: section callback %*ph\n", 6, buffer1);
	ret = dvb_dmxdev_buffer_write(&dmxdevfilter->buffer, buffer1,
				      buffer1_len);
	if (dmxdevfilter->params.sec.pid == section_write_debug || section_write_debug == 1) {
		pr_err("pid=0x%04x, tid=0x%04x, len:%5d, tsid:0x%04x, ver:%5d, sec_num:%5d, last:%5d\n",
			dmxdevfilter->params.sec.pid , buffer1[0], ((buffer1[1] & 0xf) << 8 | buffer1[2]) + 3,
			(buffer1[3] << 8) | buffer1[4], (buffer1[5] & 0x3e) >> 1, buffer1[6], buffer1[7]);
#ifdef CONFIG_DVB_LINKED_RBUF
		if (rb_debug) {
			pr_err("pid=0x%04x, tid=0x%04x, avail = %d\n",
				dmxdevfilter->params.sec.pid, buffer1[0],
				dvb_llbuf_avail(&dmxdevfilter->buffer));
		}
#endif
	}
	if (ret == (int)buffer1_len) {
		ret = dvb_dmxdev_buffer_write(&dmxdevfilter->buffer, buffer2,
					      buffer2_len);
	}
	if (ret < 0)
		dmxdevfilter->buffer.error = ret;
	if (dmxdevfilter->params.sec.flags & DMX_ONESHOT)
		dmxdevfilter->state = DMXDEV_STATE_DONE;
	spin_unlock(&dmxdevfilter->dev->lock);
	wake_up(&dmxdevfilter->buffer.queue);
	return 0;
}

static int dvb_dmxdev_ts_callback(const u8 *buffer1, size_t buffer1_len,
				  const u8 *buffer2, size_t buffer2_len,
				  struct dmx_ts_feed *feed,
				  enum dmx_success success)
{
	struct dmxdev_filter *dmxdevfilter = feed->priv;
#ifdef CONFIG_DVB_LINKED_RBUF
	struct dvb_llbuffer *buffer;
#else
	struct dvb_ringbuffer *buffer;
#endif
	int ret;

	spin_lock(&dmxdevfilter->dev->lock);
	if (dmxdevfilter->params.pes.output == DMX_OUT_DECODER) {
		spin_unlock(&dmxdevfilter->dev->lock);
		return 0;
	}

	if (dmxdevfilter->params.pes.output == DMX_OUT_TAP
	    || dmxdevfilter->params.pes.output == DMX_OUT_TSDEMUX_TAP)
		buffer = &dmxdevfilter->buffer;
	else
		buffer = &dmxdevfilter->dev->dvr_buffer;
	if (buffer->error) {
		spin_unlock(&dmxdevfilter->dev->lock);
		wake_up(&buffer->queue);
		return 0;
	}
	ret = dvb_dmxdev_buffer_write(buffer, buffer1, buffer1_len);
	if ((dmxdevfilter->params.pes.pid == pes_write_debug || pes_write_debug == 1)
		&& dmxdevfilter->params.pes.output == DMX_OUT_TAP) {
		pr_err("ts_callback: pid(%d), buffer=%p, dmxdevfilter=0x%x, buf1_len=%d, output=%d, ret=%d\n",
			dmxdevfilter->params.pes.pid, buffer, (unsigned int) dmxdevfilter,
			buffer1_len, dmxdevfilter->params.pes.output, ret); 
#ifdef CONFIG_DVB_LINKED_RBUF
		if (rb_debug) {
			pr_err("pid=0x%04x, avail = %d\n", dmxdevfilter->params.pes.pid,
				dvb_llbuf_avail(buffer));
		}
#endif
	}
	if (dvr_debug && dmxdevfilter->params.pes.output == DMX_OUT_TS_TAP) {
		pr_err("ts_dvr_callback: buffer=%p, dmxdevfilter=0x%x, buf1_len=%d, output=%d, ret=%d\n",
			buffer, (unsigned int) dmxdevfilter, buffer1_len,
			dmxdevfilter->params.pes.output, ret);
#ifdef CONFIG_DVB_LINKED_RBUF
		if (rb_debug) {
			pr_err("dvr avail = %d\n", dvb_llbuf_avail(buffer));
		}
#endif
	}
	if (ret == (int)buffer1_len)
		ret = dvb_dmxdev_buffer_write(buffer, buffer2, buffer2_len);
	if (ret < 0)
		buffer->error = ret;
	spin_unlock(&dmxdevfilter->dev->lock);
	wake_up(&buffer->queue);
	return 0;
}

static int dvb_dmxdev_ts_hdr_callback(const u8 *buffer1, size_t buffer1_len,
				  const u8 *buffer2, size_t buffer2_len,
				  struct dmx_ts_feed *feed,
				  enum dmx_success success)
{
	struct dmxdev_filter *dmxdevfilter = feed->priv;
#ifdef CONFIG_DVB_LINKED_RBUF
	struct dvb_llbuffer *buffer;
#else
	struct dvb_ringbuffer *buffer;
#endif
	int ret;

	spin_lock(&dmxdevfilter->dev->lock);
	if (dmxdevfilter->params.pes.output == DMX_OUT_DECODER) {
		spin_unlock(&dmxdevfilter->dev->lock);
		return 0;
	}

	buffer = &dmxdevfilter->dev->dvr_hdr_buffer;
	if (buffer->error) {
		spin_unlock(&dmxdevfilter->dev->lock);
		wake_up(&buffer->queue);
		return 0;
	}

#ifdef CONFIG_DVB_LINKED_RBUF
	if (rb_debug) {
		pr_err("hdr_callback: hdr_buffer=%p, count=%d, avail = %d\n",
			buffer, buffer1_len, dvb_llbuf_avail(buffer));
	}
#endif
	ret = dvb_dmxdev_buffer_write(buffer, buffer1, buffer1_len);
	if (ret == (int)buffer1_len)
		ret = dvb_dmxdev_buffer_write(buffer, buffer2, buffer2_len);
	if (ret < 0)
		buffer->error = ret;

	spin_unlock(&dmxdevfilter->dev->lock);
	wake_up(&buffer->queue);
	return 0;
}

/* stop feed but only mark the specified filter as stopped (state set) */
static int dvb_dmxdev_feed_stop(struct dmxdev_filter *dmxdevfilter)
{
	struct dmxdev_feed *feed;

	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
		del_timer(&dmxdevfilter->timer);
		dmxdevfilter->feed.sec->stop_filtering(dmxdevfilter->feed.sec);
		break;
	case DMXDEV_TYPE_PES:
		list_for_each_entry(feed, &dmxdevfilter->feed.ts, next)
			feed->ts->stop_filtering(feed->ts);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* start feed associated with the specified filter */
static int dvb_dmxdev_feed_start(struct dmxdev_filter *filter)
{
	struct dmxdev_feed *feed;
	int ret;

	dvb_dmxdev_filter_state_set(filter, DMXDEV_STATE_GO);

	switch (filter->type) {
	case DMXDEV_TYPE_SEC:
		return filter->feed.sec->start_filtering(filter->feed.sec);
	case DMXDEV_TYPE_PES:
		list_for_each_entry(feed, &filter->feed.ts, next) {
			ret = feed->ts->start_filtering(feed->ts);
			if (ret < 0) {
				dvb_dmxdev_feed_stop(filter);
				return ret;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* restart section feed if it has filters left associated with it,
   otherwise release the feed */
static int dvb_dmxdev_feed_restart(struct dmxdev_filter *filter)
{
	int i;
	struct dmxdev *dmxdev = filter->dev;
//	u16 pid = filter->params.sec.pid;

	for (i = 0; i < dmxdev->filternum; i++)
		if (dmxdev->filter[i].state >= DMXDEV_STATE_GO &&
/*		    dmxdev->filter[i].type == DMXDEV_TYPE_SEC &&
		    dmxdev->filter[i].params.sec.pid == pid) {
			dvb_dmxdev_feed_start(&dmxdev->filter[i]);
			return 0;
*/
		 dmxdev->filter[i].type == DMXDEV_TYPE_SEC) {
			if (dmxdev->demux->match_filter(dmxdev->filter[i].feed.sec,
				&dmxdev->filter[i].params.sec,
				&filter->params.sec)) {
					dvb_dmxdev_feed_start(&dmxdev->filter[i]);
					return 0;
			}
		}

	filter->dev->demux->release_section_feed(dmxdev->demux,
						 filter->feed.sec);

	return 0;
}

static int dvb_dmxdev_filter_stop(struct dmxdev_filter *dmxdevfilter)
{
	struct dmxdev_feed *feed;
	struct dmx_demux *demux;

	if (dmxdevfilter->state < DMXDEV_STATE_GO)
		return 0;

	switch (dmxdevfilter->type) {
	case DMXDEV_TYPE_SEC:
		if (!dmxdevfilter->feed.sec)
			break;
		dvb_dmxdev_feed_stop(dmxdevfilter);
		if (dmxdevfilter->filter.sec)
			dmxdevfilter->feed.sec->
			    release_filter(dmxdevfilter->feed.sec,
					   dmxdevfilter->filter.sec);
		dvb_dmxdev_feed_restart(dmxdevfilter);
		dmxdevfilter->feed.sec = NULL;
		break;
	case DMXDEV_TYPE_PES:
		dvb_dmxdev_feed_stop(dmxdevfilter);
		demux = dmxdevfilter->dev->demux;
		list_for_each_entry(feed, &dmxdevfilter->feed.ts, next) {
			demux->release_ts_feed(demux, feed->ts);
			feed->ts = NULL;
		}
		break;
	default:
		if (dmxdevfilter->state == DMXDEV_STATE_ALLOCATED)
			return 0;
		return -EINVAL;
	}

#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_flush(&dmxdevfilter->buffer);
#else
	dvb_ringbuffer_flush(&dmxdevfilter->buffer);
#endif
	return 0;
}

static void dvb_dmxdev_delete_pids(struct dmxdev_filter *dmxdevfilter)
{
	struct dmxdev_feed *feed, *tmp;

	/* delete all PIDs */
	list_for_each_entry_safe(feed, tmp, &dmxdevfilter->feed.ts, next) {
		list_del(&feed->next);
		kfree(feed);
	}

	BUG_ON(!list_empty(&dmxdevfilter->feed.ts));
}

static inline int dvb_dmxdev_filter_reset(struct dmxdev_filter *dmxdevfilter)
{
	if (dmxdevfilter->state < DMXDEV_STATE_SET)
		return 0;

	if (dmxdevfilter->type == DMXDEV_TYPE_PES)
		dvb_dmxdev_delete_pids(dmxdevfilter);

	dmxdevfilter->type = DMXDEV_TYPE_NONE;
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_ALLOCATED);
	return 0;
}

static int dvb_dmxdev_start_feed(struct dmxdev *dmxdev,
				 struct dmxdev_filter *filter,
				 struct dmxdev_feed *feed)
{
	struct timespec timeout = { 0 };
	struct dmx_pes_filter_params *para = &filter->params.pes;
	dmx_output_t otype;
	int ret;
	int ts_type;
	enum dmx_ts_pes ts_pes;
	struct dmx_ts_feed *tsfeed;

	feed->ts = NULL;
	otype = para->output;

	ts_pes = para->pes_type;

	if (ts_pes < DMX_PES_OTHER)
		ts_type = TS_DECODER;
	else
		ts_type = 0;

	if (otype == DMX_OUT_TS_TAP)
		ts_type |= TS_PACKET;
	else if (otype == DMX_OUT_TSDEMUX_TAP)
		ts_type |= TS_PACKET | TS_DEMUX;
	else if (otype == DMX_OUT_TAP)
		ts_type |= TS_PACKET | TS_DEMUX | TS_PAYLOAD_ONLY;

	ret = dmxdev->demux->allocate_ts_feed(dmxdev->demux, &feed->ts,
					      dvb_dmxdev_ts_callback);
	if (ret < 0)
		return ret;

	if (dmxdev->demux->capabilities & DMX_TS_HEADER_PARSER) {
		ret = dmxdev->demux->add_ts_hdr_cb(dmxdev->demux, feed->ts,
				dvb_dmxdev_ts_hdr_callback);
		if (ret < 0) {
			dmxdev->demux->release_ts_feed(dmxdev->demux, feed->ts);
			return ret;
		}
	}

	tsfeed = feed->ts;
	tsfeed->priv = filter;

	ret = tsfeed->set(tsfeed, feed->pid, ts_type, ts_pes, 32768, timeout);
	if (ret < 0) {
		dmxdev->demux->release_ts_feed(dmxdev->demux, tsfeed);
		return ret;
	}

	ret = tsfeed->start_filtering(tsfeed);
	if (ret < 0) {
		dmxdev->demux->release_ts_feed(dmxdev->demux, tsfeed);
		return ret;
	}

	return 0;
}

static int dvb_dmxdev_filter_start(struct dmxdev_filter *filter)
{
	struct dmxdev *dmxdev = filter->dev;
	struct dmxdev_feed *feed;
	void *mem;
	int ret, i;

	if (filter->state < DMXDEV_STATE_SET)
		return -EINVAL;

	if (filter->state >= DMXDEV_STATE_GO)
		dvb_dmxdev_filter_stop(filter);

	if (!filter->buffer.data) {
#ifdef CONFIG_DVB_LINKED_RBUF
		mem = vmalloc(DVB_LLBUF_BLOCK_SIZE);
#else
		mem = vmalloc((unsigned long)filter->buffer.size);
#endif
		if (!mem)
			return -ENOMEM;
		spin_lock_irq(&filter->dev->lock);
		filter->buffer.data = mem;
		spin_unlock_irq(&filter->dev->lock);
	}

#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_flush(&filter->buffer);
#else
	dvb_ringbuffer_flush(&filter->buffer);
#endif

	switch (filter->type) {
	case DMXDEV_TYPE_SEC:
	{
		struct dmx_sct_filter_params *para = &filter->params.sec;
		struct dmx_section_filter **secfilter = &filter->filter.sec;
		struct dmx_section_feed **secfeed = &filter->feed.sec;

		*secfilter = NULL;
		*secfeed = NULL;


		/* find active filter/feed with same PID */
		for (i = 0; i < dmxdev->filternum; i++) {
			if (dmxdev->filter[i].state >= DMXDEV_STATE_GO &&
		/*	    dmxdev->filter[i].type == DMXDEV_TYPE_SEC &&
			    dmxdev->filter[i].params.sec.pid == para->pid) {
				*secfeed = dmxdev->filter[i].feed.sec;
				break;
		*/

		dmxdev->filter[i].type == DMXDEV_TYPE_SEC) {
                        if (dmxdev->demux->match_filter(dmxdev->filter[i].feed.sec,
                                &dmxdev->filter[i].params.sec,
                                &filter->params.sec)) {
	
							*secfeed = dmxdev->filter[i].feed.sec;
							break;
				}
			}
		}

		/* if no feed found, try to allocate new one */
		if (!*secfeed) {
			ret = dmxdev->demux->allocate_section_feed(dmxdev->demux,
								   secfeed,
								   dvb_dmxdev_section_callback);
			if (ret < 0) {
				printk("DVB (%s): could not alloc feed\n",
				       __func__);
				return ret;
			}

			ret = (*secfeed)->set(*secfeed, para->pid, 32768,
					      (para->flags & DMX_CHECK_CRC) ? 1 : 0);
			if (ret < 0) {
				printk("DVB (%s): could not set feed\n",
				       __func__);
				dvb_dmxdev_feed_restart(filter);
				return ret;
			}
		} else {
			dvb_dmxdev_feed_stop(filter);
		}

		ret = (*secfeed)->allocate_filter(*secfeed, secfilter);
		if (ret < 0) {
			dvb_dmxdev_feed_restart(filter);
			filter->feed.sec->start_filtering(*secfeed);
			dprintk("could not get filter\n");
			return ret;
		}

		(*secfilter)->priv = filter;

		memcpy(&((*secfilter)->filter_value[3]),
		       &(para->filter.filter[1]), DMX_FILTER_SIZE - 1);
		memcpy(&(*secfilter)->filter_mask[3],
		       &para->filter.mask[1], DMX_FILTER_SIZE - 1);
		memcpy(&(*secfilter)->filter_mode[3],
		       &para->filter.mode[1], DMX_FILTER_SIZE - 1);

		(*secfilter)->filter_value[0] = para->filter.filter[0];
		(*secfilter)->filter_mask[0] = para->filter.mask[0];
		(*secfilter)->filter_mode[0] = para->filter.mode[0];
		(*secfilter)->filter_mask[1] = 0;
		(*secfilter)->filter_mask[2] = 0;

		filter->todo = 0;

		ret = filter->feed.sec->start_filtering(filter->feed.sec);
		if (ret < 0)
			return ret;

		dvb_dmxdev_filter_timer(filter);
		break;
	}
	case DMXDEV_TYPE_PES:
		list_for_each_entry(feed, &filter->feed.ts, next) {
			ret = dvb_dmxdev_start_feed(dmxdev, filter, feed);
			if (ret < 0) {
				dvb_dmxdev_filter_stop(filter);
				return ret;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	dvb_dmxdev_filter_state_set(filter, DMXDEV_STATE_GO);
	return 0;
}

static int dvb_demux_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	int i;
	struct dmxdev_filter *dmxdevfilter;

	if (!dmxdev->filter)
		return -EINVAL;

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	for (i = 0; i < dmxdev->filternum; i++)
		if (dmxdev->filter[i].state == DMXDEV_STATE_FREE)
			break;

	if (i == dmxdev->filternum) {
		mutex_unlock(&dmxdev->mutex);
		return -EMFILE;
	}

	dmxdevfilter = &dmxdev->filter[i];
	mutex_init(&dmxdevfilter->mutex);
	file->private_data = dmxdevfilter;

#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_init(&dmxdevfilter->buffer, NULL, DVB_LLBUF_BLOCK_SIZE);
#else
	dvb_ringbuffer_init(&dmxdevfilter->buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);
#endif
	dmxdevfilter->type = DMXDEV_TYPE_NONE;
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_ALLOCATED);
	init_timer(&dmxdevfilter->timer);

	dvbdev->users++;

	mutex_unlock(&dmxdev->mutex);
	return 0;
}

static int dvb_dmxdev_filter_free(struct dmxdev *dmxdev,
				  struct dmxdev_filter *dmxdevfilter)
{
	mutex_lock(&dmxdev->mutex);
	mutex_lock(&dmxdevfilter->mutex);

	dvb_dmxdev_filter_stop(dmxdevfilter);
	dvb_dmxdev_filter_reset(dmxdevfilter);

	if (dmxdevfilter->buffer.data) {
		void *mem = dmxdevfilter->buffer.data;

		spin_lock_irq(&dmxdev->lock);
		dmxdevfilter->buffer.data = NULL;
		spin_unlock_irq(&dmxdev->lock);
		vfree(mem);
	}

	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_FREE);
	wake_up(&dmxdevfilter->buffer.queue);
	mutex_unlock(&dmxdevfilter->mutex);
	mutex_unlock(&dmxdev->mutex);
	return 0;
}

static inline void invert_mode(dmx_filter_t *filter)
{
	int i;

	for (i = 0; i < DMX_FILTER_SIZE; i++)
		filter->mode[i] ^= 0xff;
}

static int dvb_dmxdev_add_pid(struct dmxdev *dmxdev,
			      struct dmxdev_filter *filter, u16 pid)
{
	struct dmxdev_feed *feed;

	if ((filter->type != DMXDEV_TYPE_PES) ||
	    (filter->state < DMXDEV_STATE_SET))
		return -EINVAL;

	/* only TS packet filters may have multiple PIDs */
	if ((filter->params.pes.output != DMX_OUT_TSDEMUX_TAP) &&
	    (!list_empty(&filter->feed.ts)))
		return -EINVAL;

	feed = kzalloc(sizeof(struct dmxdev_feed), GFP_KERNEL);
	if (feed == NULL)
		return -ENOMEM;

	feed->pid = pid;
	list_add(&feed->next, &filter->feed.ts);

	if (filter->state >= DMXDEV_STATE_GO)
		return dvb_dmxdev_start_feed(dmxdev, filter, feed);

	return 0;
}

static int dvb_dmxdev_remove_pid(struct dmxdev *dmxdev,
				  struct dmxdev_filter *filter, u16 pid)
{
	struct dmxdev_feed *feed, *tmp;

	if ((filter->type != DMXDEV_TYPE_PES) ||
	    (filter->state < DMXDEV_STATE_SET))
		return -EINVAL;

	list_for_each_entry_safe(feed, tmp, &filter->feed.ts, next) {
		if ((feed->pid == pid) && (feed->ts != NULL)) {
			feed->ts->stop_filtering(feed->ts);
			filter->dev->demux->release_ts_feed(filter->dev->demux,
							    feed->ts);
			list_del(&feed->next);
			kfree(feed);
		}
	}

	return 0;
}

static int dvb_dmxdev_filter_set(struct dmxdev *dmxdev,
				 struct dmxdev_filter *dmxdevfilter,
				 struct dmx_sct_filter_params *params)
{
	dprintk("function : %s, PID=0x%04x, flags=%02x, timeout=%d\n",
		__func__, params->pid, params->flags, params->timeout);

	dvb_dmxdev_filter_stop(dmxdevfilter);

	dmxdevfilter->type = DMXDEV_TYPE_SEC;
	memcpy(&dmxdevfilter->params.sec,
	       params, sizeof(struct dmx_sct_filter_params));
	invert_mode(&dmxdevfilter->params.sec.filter);
	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);

	if (params->flags & DMX_IMMEDIATE_START)
		return dvb_dmxdev_filter_start(dmxdevfilter);

	return 0;
}

static int dvb_dmxdev_pes_filter_set(struct dmxdev *dmxdev,
				     struct dmxdev_filter *dmxdevfilter,
				     struct dmx_pes_filter_params *params)
{
	int ret;

	dvb_dmxdev_filter_stop(dmxdevfilter);
	dvb_dmxdev_filter_reset(dmxdevfilter);

	if ((unsigned)params->pes_type > DMX_PES_OTHER)
		return -EINVAL;

	dmxdevfilter->type = DMXDEV_TYPE_PES;
	memcpy(&dmxdevfilter->params, params,
	       sizeof(struct dmx_pes_filter_params));
	INIT_LIST_HEAD(&dmxdevfilter->feed.ts);

	dvb_dmxdev_filter_state_set(dmxdevfilter, DMXDEV_STATE_SET);

	ret = dvb_dmxdev_add_pid(dmxdev, dmxdevfilter,
				 dmxdevfilter->params.pes.pid);
	if (ret < 0)
		return ret;

	if (params->flags & DMX_IMMEDIATE_START)
		return dvb_dmxdev_filter_start(dmxdevfilter);

	return 0;
}

static ssize_t dvb_dmxdev_read_sec(struct dmxdev_filter *dfil,
				   struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	int result, hcount;
	int done = 0;

	if (dfil->todo <= 0) {
		hcount = 3 + dfil->todo;
		if (hcount > (int)count)
			hcount = (int)count;
		result = dvb_dmxdev_buffer_read(&dfil->buffer,
						file->f_flags & O_NONBLOCK,
						buf, (size_t)hcount, ppos);
		if (result < 0) {
			dfil->todo = 0;
			return result;
		}
		if (copy_from_user(dfil->secheader - dfil->todo, buf, (long unsigned int)result))
			return -EFAULT;
		buf += result;
		done = result;
		count -= (size_t)result;
		dfil->todo -= result;
		if (dfil->todo > -3)
			return done;
		dfil->todo = ((dfil->secheader[1] << 8) | dfil->secheader[2]) & 0xfff;
		if (!count)
			return done;
	}
	if (count > (size_t)dfil->todo)
		count = (size_t)dfil->todo;
	result = dvb_dmxdev_buffer_read(&dfil->buffer,
					file->f_flags & O_NONBLOCK,
					buf, count, ppos);
	if (result < 0)
		return result;
	dfil->todo -= result;
	return (result + done);
}

static ssize_t
dvb_demux_read(struct file *file, char __user *buf, size_t count,
	       loff_t *ppos)
{
	struct dmxdev_filter *dmxdevfilter = file->private_data;
	int ret;

	if (mutex_lock_interruptible(&dmxdevfilter->mutex))
		return -ERESTARTSYS;

	if (dmxdevfilter->type == DMXDEV_TYPE_SEC) {
		ret = dvb_dmxdev_read_sec(dmxdevfilter, file, buf, count, ppos);
		
		if ((dmxdevfilter->params.sec.pid == section_read_debug || section_read_debug == 1) && ret > 0) {
			pr_err("read section: pid(%d) read count = %d\n",
				dmxdevfilter->params.sec.pid, ret);
		}
	} else {
		ret = dvb_dmxdev_buffer_read(&dmxdevfilter->buffer,
					     file->f_flags & O_NONBLOCK,
					     buf, count, ppos);
		if ((dmxdevfilter->params.pes.pid == pes_read_debug || pes_read_debug == 1) && ret > 0) {
			pr_err("read pes: pid(%d) read count = %d\n",
				dmxdevfilter->params.pes.pid, ret);
		}
	}
	mutex_unlock(&dmxdevfilter->mutex);
	return ret;
}

static int dvb_demux_do_ioctl(struct file *file,
			      unsigned int cmd, void *parg)
{
	struct dmxdev_filter *dmxdevfilter = file->private_data;
	struct dmxdev *dmxdev = dmxdevfilter->dev;
	unsigned long arg = (unsigned long)parg;
	unsigned int nr = _IOC_NR(cmd);
	int ret = 0;

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	if (nr >= DMX_IOCTL_BASE && nr <= DMX_IOCTL_END) {
		if (!dmxdev->demux->ioctl) {
			mutex_unlock(&dmxdev->mutex);
			return -EINVAL;
		}

		ret = dmxdev->demux->ioctl(dmxdev->demux, cmd, parg);
		mutex_unlock(&dmxdev->mutex);
		return ret;
	}

	switch (cmd) {
	case DMX_START:
		scdbg_printk("DMX_START: dmxdevfilter 0x%x\n", (unsigned int)dmxdevfilter);
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			mutex_unlock(&dmxdev->mutex);
			return -ERESTARTSYS;
		}
		if (dmxdevfilter->state < DMXDEV_STATE_SET)
			ret = -EINVAL;
		else
			ret = dvb_dmxdev_filter_start(dmxdevfilter);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_STOP:

		scdbg_printk("DMX_STOP: dmxdevfilter 0x%x\n", (unsigned int)dmxdevfilter);
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			mutex_unlock(&dmxdev->mutex);
			return -ERESTARTSYS;
		}
		ret = dvb_dmxdev_filter_stop(dmxdevfilter);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_SET_FILTER:
		if (syscall_debug){
			struct dmx_sct_filter_params *params = parg;
			scdbg_printk("DMX_SET_FILTER: dmxdevfilter 0x%x, pid %d, tid %d\n", 
				(unsigned int)dmxdevfilter, params->pid, params->filter.filter[0]);
		}
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			mutex_unlock(&dmxdev->mutex);
			return -ERESTARTSYS;
		}
		ret = dvb_dmxdev_filter_set(dmxdev, dmxdevfilter, parg);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_SET_PES_FILTER:
		if (syscall_debug){
			struct dmx_pes_filter_params *params = parg;
			scdbg_printk("DMX_SET_PES_FILTER: dmxdevfilter 0x%x, pid %d, input %d, output %d, type %d\n", 
				(unsigned int)dmxdevfilter, params->pid, params->input, params->output, params->pes_type);
		}
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			mutex_unlock(&dmxdev->mutex);
			return -ERESTARTSYS;
		}
		ret = dvb_dmxdev_pes_filter_set(dmxdev, dmxdevfilter, parg);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_SET_BUFFER_SIZE:
		scdbg_printk("DMX_SET_BUFFER_SIZE: dmxdevfilter 0x%x, size %d\n", (unsigned int)dmxdevfilter, (int)arg);
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			mutex_unlock(&dmxdev->mutex);
			return -ERESTARTSYS;
		}
		ret = dvb_dmxdev_set_buffer_size(dmxdevfilter, arg);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_GET_PES_PIDS:
		if (!dmxdev->demux->get_pes_pids) {
			ret = -EINVAL;
			break;
		}
		dmxdev->demux->get_pes_pids(dmxdev->demux, parg);
		break;

	case DMX_GET_CAPS:
		if (!dmxdev->demux->get_caps) {
			ret = -EINVAL;
			break;
		}
		ret = dmxdev->demux->get_caps(dmxdev->demux, parg);
		break;

	case DMX_SET_SOURCE:
	{	
		scdbg_printk("DMX_SET_SOURCE: dmxdevfilter 0x%x, source %d\n", (unsigned int)dmxdevfilter, *(int *)parg);
		if (!dmxdev->demux->set_source) {
			ret = -EINVAL;
			break;
		}
		ret = dmxdev->demux->set_source(dmxdev->demux, parg);
	}
		break;

	case DMX_GET_STC:
		if (!dmxdev->demux->get_stc) {
			ret = -EINVAL;
			break;
		}
		ret = dmxdev->demux->get_stc(dmxdev->demux,
					     ((struct dmx_stc *)parg)->num,
					     &((struct dmx_stc *)parg)->stc,
					     &((struct dmx_stc *)parg)->base);
		break;

	case DMX_ADD_PID:
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			ret = -ERESTARTSYS;
			break;
		}
		ret = dvb_dmxdev_add_pid(dmxdev, dmxdevfilter, *(u16 *)parg);
		mutex_unlock(&dmxdevfilter->mutex);
		break;

	case DMX_REMOVE_PID:
		if (mutex_lock_interruptible(&dmxdevfilter->mutex)) {
			ret = -ERESTARTSYS;
			break;
		}
		ret = dvb_dmxdev_remove_pid(dmxdev, dmxdevfilter, *(u16 *)parg);
		mutex_unlock(&dmxdevfilter->mutex);
		break;
	case DMX_GET_STATUS:
		dmxdev->demux->get_status(dmxdev->demux, (struct dmx_status *)parg);
		scdbg_printk("DMX_GET_STATUS: dmxdevfilter 0x%x, cmd %d, data %d\n", (unsigned int)dmxdevfilter, ((struct dmx_status *)parg)->cmd, ((struct dmx_status *)parg)->data);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dmxdev->mutex);
	return ret;
}

static long dvb_demux_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_demux_do_ioctl);
}


static unsigned int dvb_demux_poll(struct file *file, poll_table *wait)
{
	struct dmxdev_filter *dmxdevfilter = file->private_data;
	unsigned int mask = 0;

	if (!dmxdevfilter)
		return -EINVAL;

	poll_wait(file, &dmxdevfilter->buffer.queue, wait);

	if (dmxdevfilter->state != DMXDEV_STATE_GO &&
	    dmxdevfilter->state != DMXDEV_STATE_DONE &&
	    dmxdevfilter->state != DMXDEV_STATE_TIMEDOUT)
		return 0;

	if (dmxdevfilter->buffer.error)
		mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

#ifdef CONFIG_DVB_LINKED_RBUF
	if (!dvb_llbuf_empty(&dmxdevfilter->buffer))
#else
	if (!dvb_ringbuffer_empty(&dmxdevfilter->buffer))
#endif
		mask |= (POLLIN | POLLRDNORM | POLLPRI);

	return mask;
}

static int dvb_demux_release(struct inode *inode, struct file *file)
{
	struct dmxdev_filter *dmxdevfilter = file->private_data;
	struct dmxdev *dmxdev = dmxdevfilter->dev;

	int ret;

	ret = dvb_dmxdev_filter_free(dmxdev, dmxdevfilter);

	mutex_lock(&dmxdev->mutex);
	dmxdev->dvbdev->users--;
	if(dmxdev->dvbdev->users==1 && dmxdev->exit==1) {
		fops_put(file->f_op);
		file->f_op = NULL;
		mutex_unlock(&dmxdev->mutex);
		wake_up(&dmxdev->dvbdev->wait_queue);
	} else
		mutex_unlock(&dmxdev->mutex);

	return ret;
}

static const struct file_operations dvb_demux_fops = {
	.owner = THIS_MODULE,
	.read = dvb_demux_read,
	.unlocked_ioctl = dvb_demux_ioctl,
	.open = dvb_demux_open,
	.release = dvb_demux_release,
	.poll = dvb_demux_poll,
	.llseek = default_llseek,
};

static struct dvb_device dvbdev_demux = {
	.priv = NULL,
	.users = 1,
	.writers = 1,
	.fops = &dvb_demux_fops
};

static int dvb_dvr_do_ioctl(struct file *file,
			    unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	struct dmx_demux *demux = dmxdev->demux;
	struct dvb_demux *dvbdemux = (struct dvb_demux *)demux;

	unsigned long arg = (unsigned long)parg;
	unsigned int nr = _IOC_NR(cmd);
	int ret = 0;

	if (nr >= DMX_IOCTL_BASE && nr <= DMX_IOCTL_END) {
		if (!dmxdev->demux->ioctl) {
			mutex_unlock(&dmxdev->mutex);
			return -EINVAL;
		}
		ret = dmxdev->demux->ioctl(dmxdev->demux, cmd, parg);
		mutex_unlock(&dmxdev->mutex);
		return ret;
	}

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case DMX_SET_BUFFER_SIZE:
		scdbg_printk("DVR - DMX_SET_BUFFER_SIZE, size %d\n", (int)parg);
		ret = dvb_dvr_set_buffer_size(dmxdev, arg);
		break;

//	case DVR_SET_DATA_TYPE:
//		if (!dmxdev->scr_dvr || !dmxdev->scr_dvr->set_data_type) {
//			ret = -EINVAL;
//			break;
//		}
//		ret = dmxdev->scr_dvr->set_data_type(dmxdev->scr_dvr, *(u32 *)parg);
//		break;
	case DVR_SET_SCR:
		if (!dmxdev->scr_dvr || !dmxdev->scr_dvr->set_scr) {
			ret = -EINVAL;
			break;
		}
		ret = dmxdev->scr_dvr->set_scr(dmxdev->scr_dvr, parg);
		break;
	case DVR_SET_DESCR:
		if (!dmxdev->scr_dvr || !dmxdev->scr_dvr->set_descr) {
			ret = -EINVAL;
			break;
		}
		ret = dmxdev->scr_dvr->set_descr(dmxdev->scr_dvr, parg);
		break;

	case DMX_SET_RECORD_MODE:
		scdbg_printk("DVR - DMX_SET_RECORD_MODE, mode %d\n", arg);
		if(arg > DMX_REC_MODE_ANALOG) {
			ret = -EINVAL;
			break;
		}
		dvbdemux->record_mode = arg;
		break;
		
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dmxdev->mutex);
	return ret;
}

static long dvb_dvr_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_dvr_do_ioctl);
}

static unsigned int dvb_dvr_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	unsigned int mask = 0;

	dprintk("function : %s\n", __func__);

	poll_wait(file, &dmxdev->dvr_buffer.queue, wait);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (dmxdev->dvr_buffer.error)
			mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

#ifdef CONFIG_DVB_LINKED_RBUF
		if (!dvb_llbuf_empty(&dmxdev->dvr_buffer))
#else
		if (!dvb_ringbuffer_empty(&dmxdev->dvr_buffer))
#endif
			mask |= (POLLIN | POLLRDNORM | POLLPRI);
	} else
		mask |= (POLLOUT | POLLWRNORM | POLLPRI);

	return mask;
}

static const struct file_operations dvb_dvr_fops = {
	.owner = THIS_MODULE,
	.read = dvb_dvr_read,
	.write = dvb_dvr_write,
	.unlocked_ioctl = dvb_dvr_ioctl,
	.open = dvb_dvr_open,
	.release = dvb_dvr_release,
	.poll = dvb_dvr_poll,
	.llseek = default_llseek,
};

static struct dvb_device dvbdev_dvr = {
	.priv = NULL,
	.readers = 1,
	.users = 1,
	.fops = &dvb_dvr_fops
};

static int dvb_dvr_hdr_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
#ifdef CONFIG_DVB_LINKED_RBUF
	size_t size = DVB_LLBUF_BLOCK_SIZE;
#else
	size_t size = DVR_HDR_BUFFER_SIZE;
#endif

	dprintk("function : %s\n", __func__);

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	if (dmxdev->exit) {
		mutex_unlock(&dmxdev->mutex);
		return -ENODEV;
	}

	if (!(dmxdev->demux->capabilities & DMX_TS_HEADER_PARSER)) {
		mutex_unlock(&dmxdev->mutex);
		return -ENODEV;
	}

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		void *mem;
		if (!dvbdev->readers) {
			mutex_unlock(&dmxdev->mutex);
			return -EBUSY;
		}
		mem = vmalloc((unsigned long)size);
		if (!mem) {
			mutex_unlock(&dmxdev->mutex);
			return -ENOMEM;
		}
#ifdef CONFIG_DVB_LINKED_RBUF
		dvb_llbuf_init(&dmxdev->dvr_hdr_buffer, mem,
				size);
#else
		dvb_ringbuffer_init(&dmxdev->dvr_hdr_buffer, mem,
				size);
#endif
		dvbdev->readers--;
	}
	
	dvbdev->users++;
	mutex_unlock(&dmxdev->mutex);
	return 0;
}

static int dvb_dvr_hdr_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;

	mutex_lock(&dmxdev->mutex);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		dvbdev->readers++;
		if (dmxdev->dvr_hdr_buffer.data) {
			void *mem = dmxdev->dvr_hdr_buffer.data;
			mb();
			spin_lock_irq(&dmxdev->lock);
			dmxdev->dvr_hdr_buffer.data = NULL;
			spin_unlock_irq(&dmxdev->lock);
			vfree(mem);
#ifdef CONFIG_DVB_LINKED_RBUF
			dvb_llbuf_flush(&dmxdev->dvr_hdr_buffer);
#endif
		}
	}
	/* TODO */
	dvbdev->users--;
	if (dvbdev->users == 1 && dmxdev->exit == 1) {
		fops_put(file->f_op);
		file->f_op = NULL;
		mutex_unlock(&dmxdev->mutex);
		wake_up(&dvbdev->wait_queue);
	} else
		mutex_unlock(&dmxdev->mutex);

	return 0;
}

static ssize_t dvb_dvr_hdr_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;

	if (dmxdev->exit)
		return -ENODEV;
	if (dvr_debug) {
		pr_err("dvr_hdr_read: file=%p, count=%d\n", file, count);
	}

	return dvb_dmxdev_buffer_read(&dmxdev->dvr_hdr_buffer,
			file->f_flags & O_NONBLOCK,
			buf, count, ppos);
}

static int dvb_dvr_hdr_set_buffer_size(struct dmxdev *dmxdev,
				      unsigned long size)
{
#ifdef CONFIG_DVB_LINKED_RBUF
	struct dvb_llbuffer *buf = &dmxdev->dvr_hdr_buffer;
#else
	struct dvb_ringbuffer *buf = &dmxdev->dvr_hdr_buffer;
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	void *newmem;
	void *oldmem;

	dprintk("function : %s\n", __func__);

	if (buf->size == (ssize_t)size)
		return 0;
	if (!size)
		return -EINVAL;
		
	newmem = vmalloc(size);
	if (!newmem)
		return -ENOMEM;

	oldmem = buf->data;

	spin_lock_irq(&dmxdev->lock);
	buf->data = (u8*)newmem;
	buf->size = (ssize_t)size;

#endif
#ifdef CONFIG_DVB_LINKED_RBUF
	dvb_llbuf_reset(buf);
#else
	/* reset and not flush in case the buffer shrinks */
	dvb_ringbuffer_reset(buf);
#endif
#ifndef CONFIG_DVB_LINKED_RBUF
	spin_unlock_irq(&dmxdev->lock);

	vfree(oldmem);
#endif
	return 0;
}

static int dvb_dvr_hdr_do_ioctl(struct file *file,
			    unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	unsigned long arg = (unsigned long)parg;
	unsigned int nr = _IOC_NR(cmd);
	int ret;

	if (mutex_lock_interruptible(&dmxdev->mutex))
		return -ERESTARTSYS;

	if (nr >= DMX_IOCTL_BASE && nr <= DMX_IOCTL_END) {
		if (!dmxdev->demux->ioctl) {
			mutex_unlock(&dmxdev->mutex);
			return -EINVAL;
		}

		ret = dmxdev->demux->ioctl(dmxdev->demux, cmd, parg);
		mutex_unlock(&dmxdev->mutex);
		return ret;
	}	

	switch (cmd) {
	case DMX_SET_BUFFER_SIZE:
		ret = dvb_dvr_hdr_set_buffer_size(dmxdev, arg);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dmxdev->mutex);
	return ret;
}

static long dvb_dvr_hdr_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_dvr_hdr_do_ioctl);
}

static unsigned int dvb_dvr_hdr_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dmxdev *dmxdev = dvbdev->priv;
	unsigned int mask = 0;

	dprintk("function : %s\n", __func__);

	poll_wait(file, &dmxdev->dvr_hdr_buffer.queue, wait);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (dmxdev->dvr_hdr_buffer.error)
			mask |= (POLLIN | POLLRDNORM | POLLPRI | POLLERR);

#ifdef CONFIG_DVB_LINKED_RBUF
		if (!dvb_llbuf_empty(&dmxdev->dvr_hdr_buffer))
#else
		if (!dvb_ringbuffer_empty(&dmxdev->dvr_hdr_buffer))
#endif
			mask |= (POLLIN | POLLRDNORM | POLLPRI);
	} else
		mask |= (POLLOUT | POLLWRNORM | POLLPRI);

	return mask;
}

static const struct file_operations dvb_dvr_hdr_fops = {
	.owner = THIS_MODULE,	
	.read = dvb_dvr_hdr_read,
	.unlocked_ioctl = dvb_dvr_hdr_ioctl,
	.open = dvb_dvr_hdr_open,
	.release = dvb_dvr_hdr_release,
	.poll = dvb_dvr_hdr_poll,
	.llseek = default_llseek,
};

static struct dvb_device dvbdev_dvr_hdr = {
	.priv = NULL,
	.readers = 1,
	.users = 1,
	.fops = &dvb_dvr_hdr_fops
};

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_demux_core_debug(void);
#endif

int dvb_dmxdev_init(struct dmxdev *dmxdev, struct dvb_adapter *dvb_adapter)
{
	int i;
#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("Demux core debug",
				15, t2ddebug_demux_core_debug, NULL);
#endif
	if (dmxdev->demux->open(dmxdev->demux) < 0)
		return -EUSERS;

	dmxdev->filter = vmalloc((unsigned long)((size_t)dmxdev->filternum * sizeof(struct dmxdev_filter)));
	if (!dmxdev->filter)
		return -ENOMEM;

	mutex_init(&dmxdev->mutex);
	spin_lock_init(&dmxdev->lock);
	for (i = 0; i < dmxdev->filternum; i++) {
		dmxdev->filter[i].dev = dmxdev;
		dmxdev->filter[i].buffer.data = NULL;
#ifdef CONFIG_DVB_LINKED_RBUF
		dmxdev->filter[i].buffer.is_initialized = 0;
#endif
		dvb_dmxdev_filter_state_set(&dmxdev->filter[i],
					    DMXDEV_STATE_FREE);
	}

	dvb_register_device(dvb_adapter, &dmxdev->dvbdev, &dvbdev_demux, dmxdev,
			    DVB_DEVICE_DEMUX);
	dvb_register_device(dvb_adapter, &dmxdev->dvr_dvbdev, &dvbdev_dvr,
			    dmxdev, DVB_DEVICE_DVR);
	
#ifdef CONFIG_DVB_LINKED_RBUF
	dmxdev->dvr_buffer.is_initialized = 0;
	dvb_llbuf_init(&dmxdev->dvr_buffer, NULL, DVB_LLBUF_BLOCK_SIZE);
#else
	dvb_ringbuffer_init(&dmxdev->dvr_buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);
#endif

	if (dmxdev->demux->capabilities & DMX_TS_HEADER_PARSER) {
		dvb_register_device(dvb_adapter, &dmxdev->dvr_hdr_dvbdev,
				&dvbdev_dvr_hdr, dmxdev, DVB_DEVICE_DVR_HDR);
#ifdef CONFIG_DVB_LINKED_RBUF
		dmxdev->dvr_hdr_buffer.is_initialized = 0;
		dvb_llbuf_init(&dmxdev->dvr_hdr_buffer,
			NULL, DVB_LLBUF_BLOCK_SIZE);
#else
		dvb_ringbuffer_init(&dmxdev->dvr_hdr_buffer, NULL, DEFAULT_DMX_BUFFER_SIZE);
#endif
	}

	return 0;
}

EXPORT_SYMBOL(dvb_dmxdev_init);

void dvb_dmxdev_release(struct dmxdev *dmxdev)
{
	dmxdev->exit=1;
	if (dmxdev->dvbdev->users > 1) {
		wait_event(dmxdev->dvbdev->wait_queue,
				dmxdev->dvbdev->users==1);
	}
	if (dmxdev->dvr_dvbdev->users > 1) {
		wait_event(dmxdev->dvr_dvbdev->wait_queue,
				dmxdev->dvr_dvbdev->users==1);
	}

	dvb_unregister_device(dmxdev->dvbdev);
	dvb_unregister_device(dmxdev->dvr_dvbdev);

	if (dmxdev->demux->capabilities & DMX_TS_HEADER_PARSER) {
	if (dmxdev->dvr_hdr_dvbdev->users > 1) {
		wait_event(dmxdev->dvr_hdr_dvbdev->wait_queue,
				dmxdev->dvr_hdr_dvbdev->users==1);
	}
		dvb_unregister_device(dmxdev->dvr_hdr_dvbdev);
	}

	vfree(dmxdev->filter);
	dmxdev->filter = NULL;
	dmxdev->demux->close(dmxdev->demux);
}

#ifdef CONFIG_T2D_DEBUGD
int t2ddebug_demux_core_debug(void)
{
	long event;
	int val, val2;
	const int ID_MAX = 99;

	PRINT_T2D("[%s]\n", __func__);
	while (1) {
                PRINT_T2D("\n");
                PRINT_T2D("=================DTV==================\n");			
                PRINT_T2D(" 1 ) System call Debug Messages(on/off)\n");
                PRINT_T2D(" 2 ) PES callback Debug Messages\n");
                PRINT_T2D(" 3 ) Section callback Debug Messages\n");
                PRINT_T2D(" 4 ) Ringbuffer Debug Messages(on/off)\n");
                PRINT_T2D(" 5 ) PVR Debug Messages(on/off)\n");
                PRINT_T2D("=======================================\n");
                PRINT_T2D("%d ) exit\n", ID_MAX);	
                PRINT_T2D(" => ");
                event = t2d_dbg_get_event_as_numeric(NULL, NULL);
                PRINT_T2D("\n");	
		if (event >= 0 && event < ID_MAX) {
			switch (event) {								
			case 1:
				PRINT_T2D("Turned on(1)or turned off(0)=>");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if (val == 1) {
					PRINT_T2D("Enabling Debug print.\n");
					syscall_debug = 1;
				}
				else if (val == 0) {
					PRINT_T2D("Disabling Debug print.\n");
					syscall_debug = 0;
				}
				break;
			case 2:
				PRINT_T2D("=================PES==================\n");			
				PRINT_T2D(" 1 ) Write Debug Messages [%d]\n", pes_write_debug);
				PRINT_T2D(" 2 ) Read Debug Messages [%d]\n", pes_read_debug);
				PRINT_T2D("=======================================\n");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if (val == 1) {
					PRINT_T2D("Enable all PID(1) or Disable all PID(-1) or Enter PID =>");
					val2 = t2d_dbg_get_event_as_numeric(NULL, NULL);
					PRINT_T2D("\n");
					pes_write_debug = val2;
				} else if (val == 2) {
					PRINT_T2D("Enable all PID(1) or Disable all PID(-1) or Enter PID =>");
					val2 = t2d_dbg_get_event_as_numeric(NULL, NULL);
					PRINT_T2D("\n");
					pes_read_debug = val2;
				}
				break;

			case 3:
				PRINT_T2D("===============Section=================\n");			
				PRINT_T2D(" 1 ) Write Debug Messages [%d]\n", section_write_debug);
				PRINT_T2D(" 2 ) Read Debug Messages [%d]\n", section_read_debug);
				PRINT_T2D("=======================================\n");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if (val == 1) {
					PRINT_T2D("Enable all PID(1) or Disable all PID(-1) or Enter PID =>");
					val2 = t2d_dbg_get_event_as_numeric(NULL, NULL);
					PRINT_T2D("\n");
					section_write_debug = val2;
				} else if (val == 2) {
					PRINT_T2D("Enable all PID(1) or Disable all PID(-1) or Enter PID =>");
					val2 = t2d_dbg_get_event_as_numeric(NULL, NULL);
					PRINT_T2D("\n");
					section_read_debug = val2;
				}
				break;
			case 4:
				PRINT_T2D("Turned on(1)or turned off(0)=>");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if (val == 1) {
					PRINT_T2D("Enabling Debug print.\n");
					rb_debug = 1;
				} else if (val == 0) {
					PRINT_T2D("Disabling Debug print.\n");
					rb_debug = 0;
				}
				break;
			case 5:
				PRINT_T2D("Turned on(1)or turned off(0)=>");
				val = t2d_dbg_get_event_as_numeric(NULL, NULL);
				PRINT_T2D("\n");
				if (val == 1) {
					PRINT_T2D("Enabling Debug print.\n");
					dvr_debug = 1;
				} else if (val == 0) {
					PRINT_T2D("Disabling Debug print.\n");
					dvr_debug = 0;
				}
				break;
			default:
				break;
			}
		} else {
			break;
		}
	}

	/* TODO: Validate return value from T2D_DBG */
	return 1;
}
#endif

EXPORT_SYMBOL(dvb_dmxdev_release);
