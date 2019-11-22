#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>  /* mmap related stuff */
#else
#include <string.h>
#include <stdio.h>
/* for mmap */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#endif
#include <agent/kern_ringbuffer.h>


/* Reset the read and write pointers to zero. This is not thread
   safe. */

void kern_ringbuffer_reset(ringbuffer_t *rb)
{
	if (!rb)
		return;

	rb->read_ptr = 0;
	rb->write_ptr = 0;
}

/* Return the number of bytes available for reading.  This is the
   number of bytes in front of the read pointer and behind the write
   pointer.  */

size_t kern_ringbuffer_read_space(ringbuffer_t *rb)
{
	size_t w, r;

	if (!rb)
		return 0;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r)
		return w - r;
	else
		return (w - r + rb->size) & rb->size_mask;
}

/* Return the number of bytes available for writing.  This is the
   number of bytes in front of the write pointer and behind the read
   pointer.  */

size_t kern_ringbuffer_write_space(ringbuffer_t *rb)
{
	size_t w, r;

	if (!rb)
		return 0;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r)
		return ((r - w + rb->size) & rb->size_mask) - 1;
	else if (w < r)
		return (r - w) - 1;
	else
		return rb->size - 1;
}


/* Check space with advance ringbuffer writer pointer */
static size_t kern_ringbuffer_adv_write_space(ringbuffer_t *rb)
{
	size_t w, r;

	if (!rb)
		return 0;

	w = rb->adv_write_ptr;
	r = rb->read_ptr;

	if (w > r)
		return ((r - w + rb->size) & rb->size_mask) - 1;
	else if (w < r)
		return (r - w) - 1;
	else
		return rb->size - 1;
}

/* The copying data reader.  Copy at most `cnt' bytes from `rb' to
   `dest'.  Returns the actual number of bytes copied. */

size_t kern_ringbuffer_read(ringbuffer_t *rb, char *dest, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_read;
	size_t n1, n2;

	if (!rb)
		return 0;

	free_cnt = kern_ringbuffer_read_space(rb);
	if (free_cnt == 0)
		return 0;

	to_read = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = rb->read_ptr + to_read;

	if (cnt2 > rb->size) {
		n1 = rb->size - rb->read_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_read;
		n2 = 0;
	}

	memcpy(dest, &(rb->buf[rb->read_ptr]), n1);
	rb->read_ptr += n1;
	rb->read_ptr &= rb->size_mask;

	if (n2) {
		memcpy(dest + n1, &(rb->buf[rb->read_ptr]), n2);
		rb->read_ptr += n2;
		rb->read_ptr &= rb->size_mask;
	}

	return to_read;
}

/* The copying data writer.  Copy at most `cnt' bytes to `rb' from
   `src'.  Returns the actual number of bytes copied. */

size_t kern_ringbuffer_write(ringbuffer_t *rb, char *src, size_t cnt)
{
#ifdef __KERNEL__
	size_t free_cnt;
	size_t cnt2;
	size_t to_write;
	size_t n1, n2;

	if (!rb)
		return 0;

	free_cnt = kern_ringbuffer_write_space(rb);
	if (free_cnt == 0)
		return 0;

	to_write = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = rb->write_ptr + to_write;

	if (cnt2 > rb->size) {
		n1 = rb->size - rb->write_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_write;
		n2 = 0;
	}

	memcpy(&(rb->buf[rb->write_ptr]), src, n1);
	rb->write_ptr += n1;
	rb->write_ptr &= rb->size_mask;

	if (n2) {
		/* rb_debug("write_ptr: %d (should be 0)\n", rb->write_ptr); */
		memcpy(&(rb->buf[rb->write_ptr]), src + n1, n2);
		rb->write_ptr += n2;
		rb->write_ptr &= rb->size_mask;
	}

	return to_write;
#else
	rb_err("User shouldn't write to ringbuffer\n");
	return 0;
#endif
}

size_t kern_ringbuffer_adv_write(ringbuffer_t *rb, char *src, size_t cnt)
{
#ifdef __KERNEL__
	size_t free_cnt;
	size_t cnt2;
	size_t to_write;
	size_t n1, n2;

	if (!rb)
		return 0;

	free_cnt = kern_ringbuffer_adv_write_space(rb);
	if (free_cnt == 0)
		return 0;

	to_write = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = rb->adv_write_ptr + to_write;

	if (cnt2 > rb->size) {
		n1 = rb->size - rb->adv_write_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_write;
		n2 = 0;
	}

	memcpy(&(rb->buf[rb->adv_write_ptr]), src, n1);
	rb->adv_write_ptr += n1;
	rb->adv_write_ptr &= rb->size_mask;

	if (n2) {
		memcpy(&(rb->buf[rb->adv_write_ptr]), src + n1, n2);
		rb->adv_write_ptr += n2;
		rb->adv_write_ptr &= rb->size_mask;
	}

	return to_write;
#else
	rb_err("User shouldn't write to ringbuffer\n");
	return 0;
#endif
}

/* Advance the read pointer `cnt' places. */

void kern_ringbuffer_read_advance(ringbuffer_t *rb, size_t cnt)
{
	if (!rb)
		return;

	rb->read_ptr += cnt;
	rb->read_ptr &= rb->size_mask;
}

/* Advance the write pointer `cnt' places. */

void kern_ringbuffer_write_advance(ringbuffer_t *rb, size_t cnt)
{
#ifdef __KERNEL__
	if (!rb)
		return;

	rb->write_ptr += cnt;
	rb->write_ptr &= rb->size_mask;
#else
	rb_err("User shouldn't write to ringbuffer\n");
	return;
#endif
}

void kern_ringbuffer_inc_adv_writer(ringbuffer_t *rb, size_t cnt)
{
#ifdef __KERNEL__
	if (!rb)
		return;

	rb->adv_write_ptr += cnt;
	rb->adv_write_ptr &= rb->size_mask;
#else
	rb_err("User shouldn't write to ringbuffer\n");
	return;
#endif
}

int kern_ringbuffer_reset_adv_writer(ringbuffer_t *rb)
{
#ifdef __KERNEL__
	if (!rb)
		return -1;

	rb->adv_write_ptr = rb->write_ptr;
	return 0;
#else
	rb_err("User shouldn't write to ringbuffer\n");
	return -1;
#endif
}

void kern_ringbuffer_get_read_vec(ringbuffer_t *rb, kern_rb_data_t *vec)
{
	size_t free_cnt;
	size_t cnt2;
	size_t w, r;

	if (!rb)
		return;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r)
		free_cnt = w - r;
	else
		free_cnt = (w - r + rb->size) & rb->size_mask;

	cnt2 = r + free_cnt;

	if (cnt2 > rb->size) {

		/* Two part vector: the rest of the buffer
		 * after the current write ptr, plus some
		 * from the start of the buffer. */

		vec[0].buf = &(rb->buf[r]);
		vec[0].len = rb->size - r;
		vec[1].buf = rb->buf;
		vec[1].len = cnt2 & rb->size_mask;

	} else {

		/* Single part vector: just the rest of the buffer */

		vec[0].buf = &(rb->buf[r]);
		vec[0].len = free_cnt;
		vec[1].len = 0;
	}
}

/* The non-copying data writer.  `vec' is an array of two places.  Set
   the values at `vec' to hold the current writeable data at `rb'.  If
   the writeable data is in one segment the second segment has zero
   length.  */

void kern_ringbuffer_get_write_vec(ringbuffer_t *rb, kern_rb_data_t *vec)
{
	size_t free_cnt;
	size_t cnt2;
	size_t w, r;

	if (!rb)
		return;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r)
		free_cnt = ((r - w + rb->size) & rb->size_mask) - 1;
	else if (w < r)
		free_cnt = (r - w) - 1;
	else
		free_cnt = rb->size - 1;

	cnt2 = w + free_cnt;

	if (cnt2 > rb->size) {

		/* Two part vector: the rest of the buffer
		 * after the current write ptr, plus some
		 * from the start of the buffer. */

		vec[0].buf = &(rb->buf[w]);
		vec[0].len = rb->size - w;
		vec[1].buf = rb->buf;
		vec[1].len = cnt2 & rb->size_mask;
	} else {
		vec[0].buf = &(rb->buf[w]);
		vec[0].len = free_cnt;
		vec[1].len = 0;
	}
}

/* return the ringbuffer id corresponding to the buffer */
ringbuffer_t *kern_ringbuffer_open(const char *name, int size)
{
#ifdef __KERNEL__
	ringbuffer_t *buf = get_rb_id_by_name(name);
	if (buf && !kern_ringbuffer_reader_dead(buf)) {
		rb_debug("Ringbuffer available - ptr: %p\n", buf);
		return buf;
	} else {
		/* rb_err("Ringbuffer not created.. buf: %p\n", buf); */
		return NULL;
	}
#else
	ringbuffer_t *rb = NULL;
	int configfd = -1;

	configfd = open(name, O_RDWR);
	if (configfd < 0) {
		perror("open");
		return NULL;
	}

	rb = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, configfd, 0);
	if (rb == MAP_FAILED) {
		perror("mmap");
		close(configfd);
		return NULL;
	}
	rb->user_fd = configfd;
	rb_debug("FD: %d\n", rb->user_fd);
	return rb;
#endif
}

int kern_ringbuffer_close(ringbuffer_t *rb)
{
#ifdef __KERNEL__
	return 0;
#else
	int ret = 0;
	int fd;

	fd = rb->user_fd;
	rb_debug("FD: %d\n", fd);
	if (fd < 0) {
		rb_err("Invalid fd\n");
		return -1;
	}

	ret = munmap(rb, rb->size);
	if (ret)
		perror("munmap");
	rb_debug("Closing kern mmaped file\n");
	close(fd);
	rb_debug("Closed kern mmaped file\n");
	return 0;
#endif
}

int kern_ringbuffer_reader_dead(ringbuffer_t *rb)
{
	if (rb->user_fd == -1) {
		/* rb_debug("reader dead..\n"); */
		return 1;
	} else {
		/* rb_debug("reader alive (fd: %d)..\n", rb->user_fd); */
		return 0;
	}
}
