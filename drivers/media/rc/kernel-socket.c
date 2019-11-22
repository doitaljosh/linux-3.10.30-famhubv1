/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * @file kernel-socket.c
 * @Kernel module for TCP/IP Socket communication in Kernel space.
 * @date   2014/09/25
 *
 */



#include <linux/module.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/mutex.h>
#include "kernel-socket.h"
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>



#define sock_mutex_lock(x)		mutex_lock(x)
#define sock_mutex_unlock(x)		mutex_unlock(x)


MODULE_DESCRIPTION("Kernel space Socket Communication");
MODULE_LICENSE("GPL");

kernel_socket_t kernel_tcp_socket(int domain, int type, int protocol)
{
	struct socket *sk = NULL;
	int ret = 0;
	ret = sock_create(domain, type, protocol, &sk);
	if (ret < 0)
	{
		printk("sock_create failed\n");
		return NULL;
	}
	return sk;
}

int kernel_tcp_bind(kernel_socket_t socket, struct sockaddr *address, int address_len)
{
	struct socket *sk;
	int ret = 0;
	sk = (struct socket *)socket;
	ret = sk->ops->bind(sk, address, address_len);
	
	return ret;
}

int kernel_tcp_listen(kernel_socket_t socket, int backlog)
{
	struct socket *sk;
	int ret;
	sk = (struct socket *)socket;
	
	if ((unsigned)backlog > SOMAXCONN)
		backlog = SOMAXCONN;
	
	ret = sk->ops->listen(sk, backlog);
	
	return ret;
}

int kernel_tcp_connect(kernel_socket_t socket, struct sockaddr *address, int address_len)
{
	struct socket *sk;
	int ret;

	sk = (struct socket *)socket;
	ret = sk->ops->connect(sk, address, address_len, 0);
	
	return ret;
}

kernel_socket_t kernel_tcp_accept(kernel_socket_t socket, struct sockaddr *address, int *address_len)
{
	struct socket *sk;
	struct socket *new_sk = NULL;
	int ret;
	
	sk = (struct socket *)socket;

	
	ret = sock_create(sk->sk->sk_family, sk->type, sk->sk->sk_protocol, &new_sk);
	if (ret < 0)
		return NULL;
	if (!new_sk)
		return NULL;
	
	new_sk->type = sk->type;
	new_sk->ops = sk->ops;
	
	ret = sk->ops->accept(sk, new_sk, 0);
	if (ret < 0)
		goto error_kaccept;
	
	if (address)
	{
		ret = new_sk->ops->getname(new_sk, address, address_len, 2);
		if (ret < 0)
			goto error_kaccept;
	}
	
	return new_sk;

error_kaccept:
	sock_release(new_sk);
	return NULL;
}

ssize_t kernel_tcp_recv(kernel_socket_t socket, void *buffer, size_t length, int flags)
{
	struct socket *sk;
	struct msghdr msg;
	struct iovec iov;
	int ret;
	mm_segment_t old_fs;

	sk = (struct socket *)socket;

	iov.iov_base = (void *)buffer;
	iov.iov_len = (__kernel_size_t)length;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sock_recvmsg(sk, &msg, length, flags);
	set_fs(old_fs);

	if (ret < 0)
		goto out_krecv;
	
out_krecv:
	return ret;

}

ssize_t kernel_tcp_send(kernel_socket_t socket, const void *buffer, size_t length, int flags)
{
	struct socket *sk;
	struct msghdr msg;
	struct iovec iov;
	int len;
 	mm_segment_t old_fs;

	sk = (struct socket *)socket;

	iov.iov_base = (void *)buffer;
	iov.iov_len = (__kernel_size_t)length;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	msg.msg_flags = flags;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	len = sock_sendmsg(sk, &msg, length);
	set_fs(old_fs);
	
	return len;
}

int kernel_tcp_close(kernel_socket_t socket)
{
	struct socket *sk;
	int ret;

	sk = (struct socket *)socket;
	ret = sk->ops->release(sk);

	if (sk)
		sock_release(sk);

	return ret;
}

unsigned int kernel_tcp_inet_addr(char* ip)
{
	int a, b, c, d;
	char addr[4];
	
	sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
	addr[0] = a;
	addr[1] = b;
	addr[2] = c;
	addr[3] = d;
	
	return *(unsigned int *)addr;
}

char* kernel_tcp_inet_ntoa(struct in_addr *in)
{
	char* str_ip = NULL;
	u_int32_t int_ip = 0;
	
	str_ip = kmalloc(16 * sizeof(char), GFP_KERNEL);
	if (!str_ip)
		return NULL;
	else
		memset(str_ip, 0, 16);

	int_ip = in->s_addr;
	
	sprintf(str_ip, "%d.%d.%d.%d",  (int_ip      ) & 0xFF,
									(int_ip >> 8 ) & 0xFF,
									(int_ip >> 16) & 0xFF,
									(int_ip >> 24) & 0xFF);
	return str_ip;
}

static int kernel_tcp_socket_init(struct platform_device *pdev)
{
	printk("\n Socket Core Init \n");

	return 0;
}

static void kernel_tcp_socket_exit(struct platform_device *pdev)
{
	printk("Socket core exit\n");
}

int kernel_start_tcp_cli(void *arg)
{
	kernel_socket_t sockfd_cli;
	struct sockaddr_in addr_srv;
	char *duf, *tmp;
	int addr_len;
	struct rw_handle* rw_hnd;
	struct fifo_handle* hnd ;
	int tryConnect = 200;

	rw_hnd= (struct rw_handle*)arg;
	hnd	= rw_hnd->senderFifo;
	sprintf(current->comm, "kernel_skt_cli_%d", rw_hnd->port);	

	memset(&addr_srv, 0, sizeof(addr_srv));
	addr_srv.sin_family = AF_INET;
	addr_srv.sin_port = htons(rw_hnd->port);
	//addr_srv.sin_addr.s_addr = kernel_tcp_inet_addr("192.168.1.1");
	addr_srv.sin_addr.s_addr = kernel_tcp_inet_addr("10.123.175.1");
	
	addr_len = sizeof(struct sockaddr_in);
	
	sockfd_cli = kernel_tcp_socket(AF_INET, SOCK_STREAM, 0);
	printk("sockfd_cli = 0x%p\n", sockfd_cli);
	if (sockfd_cli == NULL)
	{
		printk("socket failed\n");
		return -1;
	}
	while (tryConnect > 0)
	{		
		if (kernel_tcp_connect(sockfd_cli, (struct sockaddr*)&addr_srv, addr_len) < 0)
		{
			printk("connect failed \n");
			tryConnect--;
			msleep(100); //Avoid tight loop 
		}
		else
		{
			printk("connect Successful\n");
			break;
		}
	}
	
	if (tryConnect <= 0)
	{
		return -1;
	}

	tmp = kernel_tcp_inet_ntoa(&addr_srv.sin_addr);
	printk("connected to : %s %d\n", tmp, ntohs(addr_srv.sin_port));
	kfree(tmp);

	duf = kmalloc((rw_hnd->protocol_msg_size) * (sizeof(char)), GFP_KERNEL);
	//Client working in Sender Mode
	while (1)
	{
         if(kthread_should_stop())
            break;
		memset(duf, 0, rw_hnd->protocol_msg_size);
		dequeue_msg(duf, rw_hnd->protocol_msg_size, hnd);
		kernel_tcp_send(sockfd_cli, duf, rw_hnd->protocol_msg_size, 0);
	}
	kernel_tcp_close(sockfd_cli);
	kfree(duf);
	
	return 0;
}

int kernel_start_tcp_srv(void *arg)
{
	kernel_socket_t sockfd_srv, sockfd_cli;
	struct sockaddr_in addr_srv;
	struct sockaddr_in addr_cli;
	char *tmp, *duf;
	int addr_len, len;
	struct rw_handle* rw_hnd;
	struct fifo_handle* hnd ;

	rw_hnd= (struct rw_handle*)arg;
	hnd	= rw_hnd->readerFifo;
	sprintf(current->comm, "kernel_skt_srv_%d", rw_hnd->port);

	
	sockfd_srv = sockfd_cli = NULL;
	memset(&addr_cli, 0, sizeof(addr_cli));
	memset(&addr_srv, 0, sizeof(addr_srv));
	addr_srv.sin_family = AF_INET;
	addr_srv.sin_port = htons( rw_hnd->port);
	addr_srv.sin_addr.s_addr = INADDR_ANY;
	addr_len = sizeof(struct sockaddr_in);
	
	
	sockfd_srv = kernel_tcp_socket(AF_INET, SOCK_STREAM, 0);
	printk("sockfd_srv = 0x%p\n", sockfd_srv);
	if (sockfd_srv == NULL)
	{
		printk("socket failed\n");
		return -1;
	}
	if (kernel_tcp_bind(sockfd_srv, (struct sockaddr *)&addr_srv, addr_len) < 0)
	{
		printk("bind failed\n");
		return -1;
	}

	if (kernel_tcp_listen(sockfd_srv, 10) < 0)
	{
		printk("listen failed\n");
		return -1;
	}

	sockfd_cli = kernel_tcp_accept(sockfd_srv, (struct sockaddr *)&addr_cli, &addr_len);


	
	if (sockfd_cli == NULL)
	{
		printk("accept failed\n");
		return -1;
	}
	else
	{
		printk("sockfd_cli = 0x%p\n", sockfd_cli);
	}

	
	tmp = kernel_tcp_inet_ntoa(&addr_cli.sin_addr);
	printk("got connected from : %s %d\n", tmp, ntohs(addr_cli.sin_port));
	kfree(tmp);

	duf = kmalloc((rw_hnd->protocol_msg_size) * (sizeof(char)), GFP_KERNEL);
	//Server working in reciever Mode
	while (1)
	{
	    if(kthread_should_stop())
            break;
		
		memset(duf, 0, rw_hnd->protocol_msg_size);
		len = kernel_tcp_recv(sockfd_cli, duf, rw_hnd->protocol_msg_size, 0);
		if (len > 0)
		{
			enqueue_msg(duf, rw_hnd->protocol_msg_size, hnd);
		}
	}
	kfree(duf);
	kernel_tcp_close(sockfd_cli);
	kernel_tcp_close(sockfd_srv);
	
	return 0;
}


int enqueue_msg (char * buffer,  int len, struct fifo_handle* hnd)
{
	int ret= 0;
	sock_mutex_lock(&(hnd->sem));
	ret = kfifo_in(&(hnd->fifo), buffer, len);
	hnd->msgpresent = 1;
	sock_mutex_unlock(&(hnd->sem));	
	wake_up_interruptible(&(hnd->wq));
	return ret;
}

int dequeue_msg (char * buffer,  int len,  struct fifo_handle* hnd)
{
	int ret =0;
	wait_event_interruptible(hnd->wq, ((hnd->msgpresent)==1));
	sock_mutex_lock(&(hnd->sem));
	ret = kfifo_out(&(hnd->fifo), buffer, len);
	if(kfifo_is_empty(&(hnd->fifo)) )
	{
		hnd->msgpresent = 0;
	}
	sock_mutex_unlock(&(hnd->sem));
	return ret;
}

struct rw_handle* register_with_socket_core( int mode, int port, int protocol_msg_size)
{
	struct rw_handle* handle = NULL;
	int ret;

	/*Allocate Complete Data structure for read-write & Synchronization*/
	
	handle = kmalloc(sizeof(struct rw_handle), GFP_KERNEL);

	if (handle)
	{
		handle->port = port;
		handle->protocol_msg_size = protocol_msg_size;
		switch (mode)
		{
			case SENDER_MODE:
				handle->senderFifo = kmalloc(sizeof(struct fifo_handle), GFP_KERNEL);	
				ret = kfifo_alloc(&((handle->senderFifo)->fifo), FIFO_SIZE, GFP_KERNEL);
				

				if (ret) {
					printk(KERN_ERR "register_with_socket_core : error kfifo_alloc\n");
					return NULL;
				}
				mutex_init(&((handle->senderFifo)->sem));
				init_waitqueue_head(&((handle->senderFifo)->wq));
				(handle->senderFifo)->msgpresent = 0;
				handle->readerFifo = NULL;
			break;

			case READER_MODE:
				handle->readerFifo = kmalloc(sizeof(struct fifo_handle), GFP_KERNEL);		
				ret = kfifo_alloc(&((handle->readerFifo)->fifo), FIFO_SIZE, GFP_KERNEL);
				if (ret) {
					printk(KERN_ERR "register_with_socket_core : error kfifo_alloc\n");
					return NULL;
				}
				mutex_init(&((handle->readerFifo)->sem));
				init_waitqueue_head(&((handle->readerFifo)->wq));
				(handle->readerFifo)->msgpresent = 0;
				handle->senderFifo= NULL;
			break;

			default:
				handle->senderFifo = kmalloc(sizeof(struct fifo_handle), GFP_KERNEL);	
				ret = kfifo_alloc(&((handle->senderFifo)->fifo), FIFO_SIZE, GFP_KERNEL);
				handle->readerFifo = kmalloc(sizeof(struct fifo_handle), GFP_KERNEL);		
				ret = kfifo_alloc(&((handle->readerFifo)->fifo), FIFO_SIZE, GFP_KERNEL);
				mutex_init(&((handle->senderFifo)->sem));
				mutex_init(&((handle->readerFifo)->sem));
				init_waitqueue_head(&((handle->senderFifo)->wq));
				init_waitqueue_head(&((handle->readerFifo)->wq));
				(handle->senderFifo)->msgpresent = 0;
				(handle->readerFifo)->msgpresent = 0;
			break;
				
		}
	}
	return handle;
}


int unregister_with_socket_core( struct rw_handle* rw_hnd)
{
	if(rw_hnd)
	{
		if (rw_hnd->senderFifo)
		{
			kfifo_free(&((rw_hnd->senderFifo)->fifo));
			mutex_destroy(&((rw_hnd->senderFifo)->sem));
			kfree(rw_hnd->senderFifo);	
		}
		
		if(rw_hnd->readerFifo)
		{
			kfifo_free(&((rw_hnd->readerFifo)->fifo));
			mutex_destroy(&((rw_hnd->readerFifo)->sem));
			kfree(rw_hnd->readerFifo);	
		}
		kfree(rw_hnd);
	}
	
	
	return 0;
}




static const struct of_device_id kernel_tcp_socket_of_match[] = {
	{ .compatible = "samsung,ksocket" },
	{},
};


static struct platform_driver kernel_tcp_socket_driver = {
	.probe = kernel_tcp_socket_init,
	.remove = kernel_tcp_socket_exit,
	.driver = {
		.name = "kernel_tcp_socket",
		.owner = THIS_MODULE,
		.of_match_table	= kernel_tcp_socket_of_match,
	},
};


module_platform_driver(kernel_tcp_socket_driver);



EXPORT_SYMBOL(kernel_start_tcp_cli);
EXPORT_SYMBOL(kernel_start_tcp_srv);
EXPORT_SYMBOL(register_with_socket_core);
EXPORT_SYMBOL(unregister_with_socket_core);
EXPORT_SYMBOL(enqueue_msg);
EXPORT_SYMBOL(dequeue_msg);















