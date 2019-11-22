#include <linux/fs.h>
#include <linux/debugfs.h>

#include <linux/mm.h>  /* mmap related stuff */
#include <linux/slab.h>
#include "agent/kern_ringbuffer.h"
#include <linux/string.h>

ringbuffer_t *kern_ringbuffer_create(unsigned long pages)
{
	ringbuffer_t *rb;
	int offset = offsetof(ringbuffer_t, buf);
	char *mem = kmalloc((pages + 2) * PAGE_SIZE, GFP_KERNEL);
	unsigned long i = 0;

	if (!mem)
		return NULL;

	rb = (ringbuffer_t *)((((unsigned long)mem) + PAGE_SIZE - 1)
							& PAGE_MASK);

	printk("rb: %p, offset: %d, buff addr: %p\n", rb, offset, rb->buf);

	rb->size = pages << PAGE_SHIFT;
	rb->size_mask = rb->size;
	rb->size_mask -= 1;
	rb->write_ptr = 0;
	rb->read_ptr = 0;
	rb->act_addr = mem;
	rb->user_fd = -1;

	/* map 1 page more for header  */
	for (i = 0; i <= pages * PAGE_SIZE; i += PAGE_SIZE) {
		SetPageReserved(virt_to_page(((unsigned long) rb) + i));
	}

	return rb;
}

/* Free all data associated with the ringbuffer `rb'. */

void kern_ringbuffer_free(ringbuffer_t *rb)
{
	unsigned long pages;
	unsigned long i = 0;

	rb_debug("Freeing the RB memory\n");
	/* multiple mmaps can have different size */
	pages = MAX_RB_SIZE >> PAGE_SHIFT;

	for (i = 0; i <= pages * PAGE_SIZE; i += PAGE_SIZE)
		ClearPageReserved(virt_to_page(((unsigned long) rb) + i));
	kfree(rb->act_addr);
}

