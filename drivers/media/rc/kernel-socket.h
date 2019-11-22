/**
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * @file kernel-socket.h
 * @Kernel module for TCP/IP Socket communication in Kernel space.
 * @date   2014/09/25
 *
 */

#ifndef _kernel_socket_h_
#define _kernel_socket_h_
#include <linux/kfifo.h>
#include <linux/wait.h>
struct socket;
struct sockaddr;
struct in_addr;
typedef struct socket * kernel_socket_t;


#define FIFO_SIZE	128
#define SENDER_MODE	1
#define READER_MODE	2

struct fifo_handle
{
	struct kfifo_rec_ptr_1 fifo;
	struct mutex sem;
	wait_queue_head_t wq;
	int msgpresent;
};

struct rw_handle
{
	struct fifo_handle* senderFifo;
	struct fifo_handle* readerFifo;
	int port;
	int protocol_msg_size;
};


/*Exported Symbols for Other Modules*/
int kernel_start_tcp_cli(void *arg);
int kernel_start_tcp_srv(void *arg);
struct rw_handle* register_with_socket_core( int mode, int port, int protocol_msg_size);
int unregister_with_socket_core( struct rw_handle* rw_hnd);
int enqueue_msg (char * buffer,  int len, struct fifo_handle* hnd);
int dequeue_msg (char* buffer,  int len,  struct fifo_handle* hnd);

/*
Steps to use
================
1. Call register_with_socket_core(), with port number, size of messages & mode(can br reader or sender).
you will get a struct rw_handle handle as a return value.
2. If mode is reader, that call kernel_start_tcp_srv() in a kernel thread, pass struct rw_handle handle
to this thread.
3. Start recieving message by using dequeue_msg & struct rw_handle handle.
4. If mode is sender, than call kernel_start_tcp_cli() in a kernel thread, pass struct rw_handle handle
to this thread.
5. Start sending mesage by using enqueue_msg & struct rw_handle handle.

Features
=======
1. Any number of client & sever connections can be made.
*/


kernel_socket_t kernel_tcp_socket(int domain, int type, int protocol);
int kernel_tcp_close(kernel_socket_t socket);
int kernel_tcp_bind(kernel_socket_t socket, struct sockaddr *address, int address_len);
int kernel_tcp_listen(kernel_socket_t socket, int backlog);
int kernel_tcp_connect(kernel_socket_t socket, struct sockaddr *address, int address_len);
kernel_socket_t kernel_tcp_accept(kernel_socket_t socket, struct sockaddr *address, int *address_len);

ssize_t kernel_tcp_recv(kernel_socket_t socket, void *buffer, size_t length, int flags);
ssize_t kernel_tcp_send(kernel_socket_t socket, const void *buffer, size_t length, int flags);
unsigned int kernel_tcp_inet_addr(char* ip);
char * kernel_inet_ntoa(struct in_addr *in);


#endif /* _kernel_socket_h_ */
