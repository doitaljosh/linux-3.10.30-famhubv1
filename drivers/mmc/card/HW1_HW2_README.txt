                           HW1 and HW2 decompression methods
                           ---------------------------------

(c) 2014 Roman Penyaev <roman.pen@samsung.com>,
    Samsung Electronics

Table of contents
-----------------

1. Overview and history
2. HW1 or sdp_unzip low-level API
3. HW2 or new block device call for decompression

1.0 Overview and history
------------------------
SDP unzip is a gzip decompression driver, which main aim is to speed up
decompression on the special SDP hardware offloading the main CPU.

For historical reasons low-level decompression API which works with hardware
directly is called HW1.  Decompression with combined reading from block device
is called HW2.  HW2 is built on top of HW1, i.e. HW2 uses API of HW1 and finally
decompresses gzip chunk calling set of SDP unzip functions.


2.0 HW1 or sdp_unzip low-level API
----------------------------------
Nowadays SDP hardware supports scatter only for output data, so as output
you have to specify array of pages and its size.  But input buffer (buffer
which is intended to be decompressed) must be physically contiguous - this
is a hardware requirement.

To use HW decompressor and to decompress gzip chunk you have to include the main
API header in your code:

  #include <mach/sdp_unzip.h>

In this header 4 main calls are defined and logically the interface is split on
synchronous and asynchronous parts:

asynchronous API:

   /* Setup HW decompressor with input buffer, output pages and your own
    * completion callback, if 0 is returned that means decompressor was
    * successfully inited */
   int sdp_unzip_decompress_async(...);

   /* If you are ready to decompress and kick the decompressor to start -
    * you have to call this function.  The result is the call of completion
    * callback from the unzip interrupt */
   void sdp_unzip_update_endpointer(void);

   /* If you decide to wait for decompression completion - call this function */
   int sdp_unzip_decompress_wait(void);

synchronous API:

   /* Under the hood this synchronous call uses asynchronous interface.
    * Synchronous variant also accepts input buffer, output pages and special
    * 'may wait' flag, if it is 'false' it tells to return with -EBUSY error
    * if HW decompressor is in progress now. */
   int sdp_unzip_decompress_sync(...);


3.0 HW2 or new block device call for decompression
---------------------------------------------------
As was told HW2 approach uses HW1 API, combining reading of gzip
compressed block from block device.  This union speeds up the overall
decompression, since the hardware makes both operations in parallel.

The basic idea of implementing HW decompressor from eMMC depends on
the hardware ability to to start decompression while reading the data
from flash has not been finished yet.  This is called 'fast path'.

Also HW2 avoids doing DMA map/unmap of input buffer, delegating this
heavy operation on HW1 interface.

The main entry point for HW2 API is a single function declared in:

  #include <linux/blkdev.h>

for block device operations:

   /* Reads scattered block from block device and starts
    * HW gzip decompression. The returning value indicates error if it is < 0
	* or amount of decompressed bytes otherwise */
   int (*hw_decompress_vec)(...);

This function pointer is set only for eMMC device, when HW1 is enabled.
The whole implementation can be found here:

   drivers/mmc/card/hw_decompress.c

'hw_decompress_vec' function accepts array of vectors, where each vector
defines physical block offset and its length.  Accepting array of IO vectors
HW2 reads and decompresses physically scattered compressed chunk, which also
provides many possibilities for higher level callers.  The output is also an
array of pages, like was described in HW1 section.

The common error, returning from 'hw_decompress_vec', is a -EBUSY, which says
that "decompressor is busy right now, please, try again later".  The assumption
behind this is simple: HW2 was designed to be as fast as possible, and if the
hardware is busy right now, or memory allocation failed - HW2 returns -EBUSY
immediately and it is better to try other ways to decompress, even to start
unzipping using the main CPU (software decompression).  Currently only two
simultaneous decompressor threads are allowed, let's consider simple chart:

HW decompression starts here and ends here
                        v             v
HW thread #1: ----------|=============|------------------------
HW thread #2: ----------|xxxxxxxxxxxxxx=============|----------
SW thread #3: ----------|+++++++++++++++++++++++++++++++|------
                        ^                               ^
SW decompression starts here                 and ends here

====  HW decompression is performed, task is in wait and scheduled
xxxx  Wait on semaphore, task is in wait and scheduled
++++  Software decompression, CPU is busy

According to simple measurements made on HawkP CPU raw hardware decompression
takes twice less time comparing to the software decompression.  So it turns
out to be that only for the second thread it is better to wait for the hardware,
others should get -EBUSY and start consuming the main CPU.

Also HW2 is scalable enough and for next more powerfull boards all the params
can be easily changed and HW2 can accept more than 2 simultaneous threads and
put them into sleep.
