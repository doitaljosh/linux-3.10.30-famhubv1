/*
 * 1999 Copyright (C) Pavel Machek, pavel@ucw.cz. This code is GPL.
 * 1999/11/04 Copyright (C) 1999 VMware, Inc. (Regis "HPReg" Duchesne)
 *            Made nbd_end_request() use the io_request_lock
 * 2001 Copyright (C) Steven Whitehouse
 *            New nbd_end_request() for compatibility with new linux block
 *            layer code.
 * 2003/06/24 Louis D. Langholtz <ldl@aros.net>
 *            Removed unneeded blksize_bits field from nbd_device struct.
 *            Cleanup PARANOIA usage & code.
 * 2004/02/19 Paul Clements
 *            Removed PARANOIA, plus various cleanup and comments
 */
#ifndef LINUX_NBD_H
#define LINUX_NBD_H


#include <linux/wait.h>
#include <linux/mutex.h>
#include <uapi/linux/nbd.h>

struct request;
struct nbd_ext_operations;

struct nbd_device {
	int flags;
	int harderror;		/* Code of hard error			*/
	struct socket * sock;
	struct file * file; 	/* If == NULL, device is not ready, yet	*/
	int magic;

	spinlock_t queue_lock;
	struct list_head queue_head;	/* Requests waiting result */
	struct request *active_req;
	wait_queue_head_t active_wq;
	struct list_head waiting_queue;	/* Requests to be sent */
	wait_queue_head_t waiting_wq;

	struct mutex tx_lock;
	struct gendisk *disk;
	int blksize;
	u64 bytesize;
	pid_t pid; /* pid of nbd-client, if attached */
	int xmit_timeout;
	int disconnect; /* a disconnect has been requested by user */

#ifdef CONFIG_NBD_KNBD_SUPPORT
	bool nbd_should_stop;
	atomic_t occupied;

	/* NBD user callbacks and methods */
	struct nbd_ext_operations *ext_ops;
	void *ext_private_data;
};

struct nbd_reply;

void nbd_restore_default_ext_ops(struct nbd_ext_operations *ext_ops);

struct nbd_ext_operations
{
       /* Callbacks */

       /* Write bvec/ Read bvec callbacks */
       void (*write_vec)(struct nbd_device *nbd, struct request *req);
       void (*read_vec)(struct nbd_device *nbd, struct request *req);
       /* Send data/ Receive data callbacks */
       void (*pre_send_bvec)(struct nbd_device *nbd, struct request *req, struct bio_vec *vec);
       void (*post_recv_bvec)(struct nbd_device *nbd, struct request *req, struct bio_vec *vec);
       /* Replies handler */
       struct request *(*handle_reply)(struct nbd_device *nbd, struct nbd_reply *reply);
       /* User requests finalizer */
       int (*finalize_request)(struct nbd_device *nbd, struct request *req);
       /* User defined packages membership predicate */
       int (*is_user_handled_request)(struct nbd_device *nbd, struct request *req);

       /* Methods */

       /* Add new request to the requests queue */
       void (*push_request)(struct nbd_device *nbd, struct request *req);
#endif
};

#endif
