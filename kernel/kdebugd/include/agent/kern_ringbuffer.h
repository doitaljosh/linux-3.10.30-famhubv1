#ifndef _KERN_RINGBUFFER_H
#define _KERN_RINGBUFFER_H

#include <linux/types.h>

#ifdef __KERNEL__
#define rb_print(fmt, args...) {                \
	printk(KERN_NOTICE                      \
		"%s:%d " fmt, \
		__func__, __LINE__, ## args);   \
}

#define rb_err(fmt, args...) {                  \
	printk(KERN_ERR                         \
		"RB Err!! %s:%d " fmt, \
		__func__, __LINE__, ## args);   \
}

#define rb_debug(fmt, args...) {                \
	printk(KERN_DEBUG                       \
		"%s:%d " fmt, \
		__func__, __LINE__, ## args);   \
}

#else
#define rb_print(fmt, args...) {                \
	printf("%s:%d " fmt,  \
		__func__, __LINE__, ##args);    \
}

#define rb_err(fmt, args...) {                  \
	printf("RB Err!! %s:%d " fmt,   \
		__func__, __LINE__, ##args);    \
}

#define rb_debug(fmt, args...) {                \
	printf("%s:%d " fmt,          \
		__func__, __LINE__, ##args);    \
}
#endif

#define MAX_RB_SIZE (128 * 1024)
typedef struct {
	volatile size_t write_ptr;
	volatile size_t read_ptr;
	volatile size_t adv_write_ptr;
	char *act_addr;
	size_t size;
	size_t size_mask;
	/* descriptor identifying the user Agent, for mmap
	 * -1 means not connected */
	int user_fd;
	char buf[0];
}
ringbuffer_t;

/**
 * ringbuffer data
 * 	To be used to avoid memcpy from ringbuffer.
 */
typedef struct {
	char *buf;
	size_t len;
}
kern_rb_data_t;

#ifdef __KERNEL__
extern ringbuffer_t *get_rb_id_by_name(const char *name);
#endif

ringbuffer_t *kern_ringbuffer_create (unsigned long sz);

/** open the ringbuffer associated with @name */
ringbuffer_t *kern_ringbuffer_open(const char *name, int sz);

void kern_ringbuffer_free (ringbuffer_t *rb);

int kern_ringbuffer_close(ringbuffer_t *rb);

void kern_ringbuffer_reset (ringbuffer_t *rb);

void kern_ringbuffer_write_advance (ringbuffer_t *rb, size_t cnt);
void kern_ringbuffer_read_advance (ringbuffer_t *rb, size_t cnt);

size_t kern_ringbuffer_write_space (ringbuffer_t *rb);
size_t kern_ringbuffer_read_space (ringbuffer_t *rb);

size_t kern_ringbuffer_read (ringbuffer_t *rb, char *dest, size_t cnt);
size_t kern_ringbuffer_write (ringbuffer_t *rb, char *src, size_t cnt);

size_t kern_ringbuffer_adv_write (ringbuffer_t *rb, char *src, size_t cnt);

void kern_ringbuffer_adv_write_ptr (ringbuffer_t *rb, size_t cnt);
int kern_ringbuffer_reset_adv_writer (ringbuffer_t *rb);
void kern_ringbuffer_get_read_vec(ringbuffer_t *rb, kern_rb_data_t *vec);
void kern_ringbuffer_get_write_vec(ringbuffer_t *rb, kern_rb_data_t *vec);
int kern_ringbuffer_reader_dead(ringbuffer_t *rb);
/* API used by kdebugd for writing in kernel RB in case of agent */
size_t kdebugd_ringbuffer_writer(void *arg, size_t size);
/* API used by kdebugd for writing data in advance in RB */
size_t kdbg_rb_adv_writer(void *arg, size_t size);
/* Advance rb write pointer after writing advance data in RB */
int kdbg_ringbuffer_write_advance(size_t size);
size_t kdbg_ringbuffer_write_space (void);
/* Set advance writer pointer of RB */
void kern_ringbuffer_inc_adv_writer (ringbuffer_t *rb, size_t cnt);
int kdbg_ringbuffer_inc_adv_writer(size_t size);
int kdbg_ringbuffer_reset_adv_writer(void);
int kdbg_ringbuffer_open(void);
int kdbg_ringbuffer_reader_dead(void);
void kdbg_ringbuffer_reset(void);
int kdbg_ringbuffer_close(void);
#endif
