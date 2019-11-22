/*
 * HW decompression extension for MMC block device
 *
 * Here all the decompression magic happens. This method
 * is historically called HW2, that means that decompression
 * is being invoked from the mmc layer, not from the upper fs
 * layer (actually hw decompression from fs layer is called HW1).
 * The basic idea of implementing hw decompressor from mmc depends
 * on the hw ability to to start decompression while reading the
 * data from flash has not been finished yet.  This is called 'fast path'.
 * Of course, fast path has requirements:
 *    1. we are the first, nobody is doing HW decompression right now
 *       (no conflicts). This is implemented by simple ref counts.
 *    2. block on disk is _not_ scattered.  The problem is that to read
 *       scattered block we have to submit several IO requests, we can't
 *       read everything just with single request.
 *
 * If fast path is not allowed (1 or 2 is false) we have to submit reading,
 * wait for the IO and only then start decompression.  This is called 'slow
 * path'.
 *
 * TODO:
 *    1. I still do not like this sequence of calls:
 *           sdp_unzip_decompress_async
 *           ....
 *           sdp_unzip_decompress_wait
 *
 *       Now it is allowed to call _async and _wait from different threads.
 *       (in case of fast path we do exactly like this: _async is called from
 *        IO completion thread, _wait is called from the caller thread).
 *       This sucks.  What has to be done is _async call must be splitted on
 *       init and async part.  Init does dma mapping and takes the lock, async
 *       part setups the hardware.  Init part must be called from the main
 *       caller (who invokes the hw_decompress_vec), but async part can be
 *       called from any context.  So, it is evident, that _wait must be called
 *       from the main caller to do dma unmap and drop the lock.
 *
 *                                                            -- roman.pen
 */
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <mach/sdp_unzip.h>

static inline void bio_partition_remap(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;

	if (bio_sectors(bio) && bdev != bdev->bd_contains) {
		struct hd_struct *p = bdev->bd_part;

		bio->bi_sector += p->start_sect;
		bio->bi_bdev = bdev->bd_contains;
	}
}

/* Check HW requirements before decompression */
static inline int __check_hw_requirements(const struct hw_iovec *vec,
					  struct hw_iovec *out_vec,
					  int i, unsigned int cnt)
{
	unsigned long long  off = vec->phys_off;
	unsigned int        len = vec->len;

	if (i == 0) {
		/* first offset must be at least aligned on 8 bytes */
		if (off & 7)
			return -EINVAL;
		len += off & 511;
		off = round_down(off, 512);
	}
	if (i == cnt - 1)
		len = round_up(len, 512);

	if ((off | len) & 511)
		return -EINVAL;

	out_vec->phys_off = off;
	out_vec->len      = len;

	return 0;
}

/*
 * hw error
 * we can distinguish from what part of the system error came
 */
enum hw_err {
	NO_ERR    = 0,
	IO_ERR,
	UNZIP_ERR
};

/*
 * hw decompress req
 */
struct hw_req {
	struct kref          kref;
	struct sdp_unzip_buf *buff;
	struct request       **reqs;
	struct completion    wait;
	atomic_t             req_cnt;
	struct page          **out_pages;
	unsigned int         out_cnt;
	sector_t             sector_off;
	unsigned int         off;
	unsigned int         compr_len;
	unsigned int         to_read_b;
	int                  result;
	enum hw_err          err;
	int                  fast;
	int                  preinited;
};

static struct request *init_req(struct block_device *bdev,
				struct hw_req *hw_req, void *payload,
				unsigned int len, unsigned long long phys_off)
{
	int err;
	struct request *req;
	struct request_queue *q = bdev_get_queue(bdev);

	req = blk_get_request(q, READ, GFP_NOWAIT);
	if (unlikely(!req))
		return ERR_PTR(-ENOMEM);

	err = blk_rq_map_kern(q, req, payload, len, GFP_NOWAIT);
	if (err) {
		blk_put_request(req);
		return ERR_PTR(err);
	}

	req->bio->bi_bdev   = bdev;
	req->bio->bi_sector = phys_off >> 9;

	/* Take partition into account */
	bio_partition_remap(req->bio);

	req->cmd_type = REQ_TYPE_SPECIAL;
	req->end_io = NULL;
	req->end_io_data = hw_req;

	req->__sector = req->bio->bi_sector;

	return req;
}

static struct hw_req *hw_req_alloc(unsigned int vec_cnt,
				   struct page **out_pages,
				   unsigned int out_cnt)
{
	int err;
	struct hw_req *hw_req;

	hw_req = kzalloc(sizeof(*hw_req), GFP_NOWAIT);
	if (!hw_req)
		return ERR_PTR(-ENOMEM);

	/* For simplicity we allocate the maximum */
	hw_req->buff = sdp_unzip_alloc(HW_MAX_IBUFF_SZ);
	if (IS_ERR(hw_req->buff)) {
		err = PTR_ERR(hw_req->buff);
		kfree(hw_req);
		return ERR_PTR(err);
	}

	hw_req->reqs = kmalloc(vec_cnt * sizeof(struct request *), GFP_NOWAIT);
	if (!hw_req->reqs) {
		sdp_unzip_free(hw_req->buff);
		kfree(hw_req);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&hw_req->kref);
	init_completion(&hw_req->wait);
	atomic_set(&hw_req->req_cnt, vec_cnt);
	hw_req->out_pages = out_pages;
	hw_req->out_cnt = out_cnt;

	return hw_req;
}

static void __hw_req_free(struct kref *kref)
{
	struct hw_req *hw_req = container_of(kref, struct hw_req, kref);
	sdp_unzip_free(hw_req->buff);
	kfree(hw_req->reqs);
	kfree(hw_req);
}

static void hw_req_put(struct hw_req *hw_req)
{
	if (IS_ERR_OR_NULL(hw_req))
		return;

	kref_put(&hw_req->kref, __hw_req_free);
}

static void rq_end_io(struct request *rq, int err)
{
	struct hw_req *hw_req = rq->end_io_data;

	/* Do not override */
	if (err && !hw_req->result) {
		hw_req->err    = IO_ERR;
		hw_req->result = -EIO;

		/* Waiter or unzip_done should see correct values */
		smp_wmb();
	}

	/* Complete on last request */
	if (atomic_dec_and_test(&hw_req->req_cnt)) {
		/* If we are on the fast path and decompressor was preinited -
		 * kick decompression to start regarding result of the IO.
		 * Here I want decompressor completion callback to be called
		 * at any case, even it will be called with unzip error */
		if (hw_req->preinited) {
			BUG_ON(!hw_req->fast);
			sdp_unzip_update_endpointer();
		}

		/* Completition, wake up IO waiter */
		complete(&hw_req->wait);
	}

	__blk_put_request(rq->q, rq);
	hw_req_put(hw_req);
}

static void unzip_done(int err, int decompressed, void *data)
{
	struct hw_req *hw_req = data;

	/* hw_req->err/result can be set by the IO thread */
	smp_rmb();

	/* Do not override IO error */
	if (err && !hw_req->result) {
		hw_req->err    = UNZIP_ERR;
		hw_req->result = -EIO;
	} else if (!err)
		hw_req->result = decompressed;

	hw_req_put(hw_req);
}

/* SW decompression twice longer than HW decompression, thus we can
 * accept only around two callers. Further callers will get -EBUSY */
static struct semaphore hw_sema = __SEMAPHORE_INITIALIZER(hw_sema,
							   HW_MAX_SIMUL_THR);

/* We need mutex for atomic IO submission and decompressor init */
static DEFINE_MUTEX(hw_mutex);

/* We have to count callers */
static atomic_t hw_callers = ATOMIC_INIT(0);

static inline void put_hw_reqs(struct request **reqs, int req_cnt)
{
	int i;

	for (i = 0; i < req_cnt; i++) {
		struct request *req = reqs[i];

		bio_put(req->bio);
		/* To shut up blk leak warning */
		req->bio = NULL;
		blk_put_request(req);
	}
}

/**
 * This must be called on fast path just before dma setup
 * of the read request.
 */
void preinit_decompressor(struct request *req)
{
	struct hw_req *hw_req = req->end_io_data;
	int may_wait = false, ret;

repeat:
	/* Setup decompressor */
	ret = sdp_unzip_decompress_async(
		hw_req->buff, hw_req->off,
		hw_req->out_pages, hw_req->out_cnt,
		unzip_done, hw_req, may_wait);
	if (ret == -EBUSY && !may_wait) {
		may_wait = true;
		pr_warn("decompressor is busy! Who uses it?\n");
		goto repeat;
	}
	/* Here we do not expect any errors */
	BUG_ON(ret);

	hw_req->preinited = true;
	kref_get(&hw_req->kref);
}

/**
 * Decompression job starts here. Logic is simple:
 *
 *  if we are alone (hw_callers == 1) and we are not doing scattered IO
 *  we can preinit decompressor and only then submit IO, thus have some
 *  perfomance benefits. If we are not alone or IO is scattered we can't
 *  do decompression and IO submission in parallel, we have to wait for
 *  the IO and only then init the decompressor. So, we are counting the
 *  callers and submit IO under the lock.
 */
static inline int do_decompression(struct hw_req *hw_req, int req_cnt)
{
	int i, callers;

	mutex_lock(&hw_mutex);

	callers = atomic_inc_return(&hw_callers);

	/* The following are HW requirements.
	 * We can't do decompressor preinition if:
	 *  1. if we are not alone, we have to do the IO and wait for it.
	 *  2. if block is scattered we have to gather it first.
	 */
	hw_req->fast = (1 == callers && 1 == req_cnt);

	/* Submit all at once */
	for (i = 0; i < req_cnt; i++) {
		struct request *req = hw_req->reqs[i];

		/* Fast path requires special preinition */
		if (hw_req->fast)
			req->special = preinit_decompressor;

		kref_get(&hw_req->kref);
		blk_execute_rq_nowait(req->q, req->rq_disk, req, 0, rq_end_io);
	}

	mutex_unlock(&hw_mutex);

	/* Wait for IO completion */
	wait_for_completion(&hw_req->wait);

	/* We do care about hw_req->err/result values */
	smp_rmb();

	/* We have to drop sdp unzip internal state by calling 'sdp_xxx_wait'
	 * if any IO error happened and request was preinited, i.e. wait must
	 * be called if sdp unzip was properly claimed before. */
	if (hw_req->err) {
		if (hw_req->preinited) {
			BUG_ON(!hw_req->fast);
			goto unzip_wait;
		}
		else
			goto out;
	}

	/* Slow path */
	if (!hw_req->fast) {
		/* Setup decompressor. Here we have to wait for other
		 * decompression task, thus may_wait is true */
		hw_req->result = sdp_unzip_decompress_async(
					hw_req->buff, hw_req->off,
					hw_req->out_pages, hw_req->out_cnt,
					unzip_done, hw_req, true);
		if (hw_req->result)
			goto out;

		kref_get(&hw_req->kref);

		/* Kick decompressor */
		sdp_unzip_update_endpointer();
	}

unzip_wait:
	/* Wait for decompression, this call implies memory barrier */
	sdp_unzip_decompress_wait();

out:
	atomic_dec(&hw_callers);

	return hw_req->result;
}

/**
 * hw_decompress_fn - decompress block on MMC using hardware
 * @bdev:	block device
 * @vec:	scattered parts of compressed block
 * @vec_cnt:	number of scattered parts
 * @out_pages:  array of output pages
 * @out_cnt:	number of output pages
 *
 * Description:
 *    If we have gzipped block on disk we can decompress it using
 *    hardware.
 *
 * Return:
 *     < 0   - error value
 *     other - decompressed size
 *
 * Note:
 *    All allocations here are being done with GFP_NOWAIT.
 *    We must be as fast as possible, so in case of memory
 *    preasure we return an -EBUSY and upper layer will do
 *    (probably) software decompression
 */
int hw_decompress_fn(struct block_device *bdev,
		     const struct hw_iovec *vec,
		     unsigned int vec_cnt,
		     struct page **out_pages,
		     unsigned int out_cnt)
{
	struct request_queue *q;
	struct hw_req *hw_req;
	int i_req = 0, ret;

	if (!out_cnt || !bdev || !out_pages || !vec || !vec_cnt)
		return -EINVAL;

	q = bdev_get_queue(bdev);
	/* This check does not make any sense at all, but all we are
	   doing is fixing prevent */
	if (!q)
		return -EINVAL;

	/* Return -EBUSY in case of contention */
	if (down_trylock(&hw_sema))
		return -EBUSY;

	hw_req = hw_req_alloc(vec_cnt, out_pages, out_cnt);
	if (IS_ERR(hw_req)) {
		ret = PTR_ERR(hw_req);
		goto out;
	}

	/* Check and init hw reqs */
	for (i_req = 0; i_req < vec_cnt; i_req++, vec++) {
		struct hw_iovec align;
		struct request *req;
		void *payload = hw_req->buff->vaddr + hw_req->to_read_b;

		ret = __check_hw_requirements(vec, &align, i_req, vec_cnt);
		if (ret)
			goto err;

		hw_req->compr_len += vec->len;
		/* Aligned length - how much we are really going to read */
		hw_req->to_read_b += align.len;

		if ((align.len >> 9) > queue_max_hw_sectors(q)) {
			ret = -EINVAL;
			goto err;
		}
		if (hw_req->to_read_b > HW_MAX_IBUFF_SZ) {
			ret = -EINVAL;
			goto err;
		}
		if (i_req == 0) {
			hw_req->off        = vec->phys_off & 511;
			hw_req->sector_off = vec->phys_off >> 9;
		}

		req = init_req(bdev, hw_req, payload,
			       align.len, align.phys_off);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			goto err;
		}

		hw_req->reqs[i_req] = req;

		continue;

err:
		/* We have to free previously allocated reqs */
		put_hw_reqs(hw_req->reqs, i_req);

		goto out;
	}

	/* Decompress */
	ret = do_decompression(hw_req, vec_cnt);

out:
	/* We have to free hw request while semaphore is down because we want
	 * to avoid any race allocations, which can happen, if we let everybody
	 * go and allocate big chunk of contiguous pages before freeing the
	 * previous one. */
	hw_req_put(hw_req);

	up(&hw_sema);

	/* -ENOMEM is not fatal, better to return -EBUSY */
	if (ret == -ENOMEM) {
		pr_err("HW_DECOMPRESS: memory problems, return -EBUSY\n");
		ret = -EBUSY;
	}

	return ret;
}
