
/******************************************************************
* 		File : sdp_messagebox.c
*		Description : 
*		Author : douk.namm@samsung.com		
*		Date : 2014/7/29
*******************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/soc.h>


#define MESSAGE_SIZE_MAX 28

#define MESSAGE_SEND 0x1
#define MESSAGE_UPDATE 0x8

#define RECVBOX_OFFSET 	(0x300)
#define SENDBOX_OFFSET 	(0x380)
#define SPIMODE_OFFSET	(0x180)
#define SPIDELAY_OFFSET	(0x190)
#define SPIERASE_OFFSET	(0x194)
#define SWRESET0_OFFSET	(0x220)


#define UPDATE_READY_DONE (0x656e6f64)

typedef struct
{
	volatile uint32_t info;
	volatile uint32_t data[7];
	volatile uint32_t reserved[8];
	volatile uint32_t pend;
	volatile uint32_t clear;
}Msg_Box_t;

typedef void (*sdp_messagebox_callback_t)(unsigned char*pBuffer, unsigned int size);


void __iomem *messagebox_base;
void __iomem *code_base;

Msg_Box_t *sendmsg = NULL;
Msg_Box_t *recvmsg = NULL;

sdp_messagebox_callback_t read_callback = NULL;

static DEFINE_MUTEX(write_mutex);

static irqreturn_t messagebox_isr(int irq, void* dev)
{	
	unsigned int msg_size = 0;
	unsigned int dst[7]= {0, 0, 0, 0, 0, 0, 0};
	unsigned int u32GicSync;

	if (recvmsg == NULL) {
		return IRQ_HANDLED;
	}
		
	if (MESSAGE_SEND & readl(&recvmsg->pend)) {	
		msg_size = readl(&recvmsg->info);
		
		if (msg_size > MESSAGE_SIZE_MAX || !msg_size) {
			writel(MESSAGE_SEND, &recvmsg->clear);
			return IRQ_HANDLED;
		}

		dst[0] = readl(&recvmsg->data[0]);
		dst[1] = readl(&recvmsg->data[1]);	
		dst[2] = readl(&recvmsg->data[2]);		
		dst[3] = readl(&recvmsg->data[3]);
		dst[4] = readl(&recvmsg->data[4]);	
		dst[5] = readl(&recvmsg->data[5]);		
		dst[6] = readl(&recvmsg->data[6]);	

		if (read_callback) {
			(*read_callback)((unsigned char*)dst, msg_size);
		}
			
		writel(MESSAGE_SEND, &recvmsg->clear);
		u32GicSync = readl(&recvmsg->clear);				
	}else {
		writel(0xff, &recvmsg->clear);
		u32GicSync = readl(&recvmsg->clear);				
	}

	return IRQ_HANDLED;
}


int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size)
{
	//Check Previous Write Operation End
	unsigned int *writebuf;
	unsigned int alignsize;
	unsigned int remainsize; 
	int i;
	int count=50;

	if (sendmsg == NULL) {
		printk(KERN_ERR "%s send msgbox addr is invalid\n", __FUNCTION__);
		return -1;
	}

	if ( size > MESSAGE_SIZE_MAX || !size ) {
		printk(KERN_ERR "%s size is too big %d\n", __FUNCTION__, size);
		return -1;		
	}

	mutex_lock(&write_mutex);
	
	do
	{
		if (!(readl(&sendmsg->pend) & MESSAGE_SEND)) {
			break;
		}
		msleep(1);
	}while(count--);

	if (!count) {
		printk(KERN_ERR "%s timeout\n", __FUNCTION__);		
		mutex_unlock(&write_mutex);
		return 0;
	}

	alignsize = size/sizeof(unsigned int); 	
	remainsize = size%sizeof(unsigned int);		
	writebuf = (unsigned int*)pbuffer;
	
	for (i=0; i < alignsize; i++) {
		writel(writebuf[i], &sendmsg->data[i]);
	}

	if ( remainsize )
	{
		writel(writebuf[i]&(0xffffffff>>(4-remainsize)), &sendmsg->data[i]);
	}

	writel(size, &sendmsg->info);
	writel(MESSAGE_SEND, &sendmsg->pend);
	mutex_unlock(&write_mutex);
					
	return size;
}
EXPORT_SYMBOL(sdp_messagebox_write);


int sdp_messagebox_write_direct(unsigned char* pbuffer, unsigned int size)
{
	//Check Previous Write Operation End
	unsigned int *writebuf;
	unsigned int alignsize;
	unsigned int remainsize;
	int i;
	int count=10;

	if (sendmsg == NULL) {
		printk(KERN_ERR "%s send msgbox addr is invalid\n", __FUNCTION__);
		return -1;
	}

	if ( size > MESSAGE_SIZE_MAX || !size ) {
		printk(KERN_ERR "%s size is too big %d\n", __FUNCTION__, size);
		return -1;
	}
	printk("send cmd for power off.\n");
	printk("micom tx pend(%d) check\n", readl(&sendmsg->pend));
	do
	{
		if (!(readl(&sendmsg->pend) & MESSAGE_SEND)) {
			break;
		}
		usleep_range(1000, 1500);
	}while(count--);
	printk("micom tx pend(%d) finish\n", readl(&sendmsg->pend));

	if (!count) {
		printk(KERN_ERR "%s timeout\n", __FUNCTION__);
		return 0;
	}

	alignsize = size/sizeof(unsigned int);
	remainsize = size%sizeof(unsigned int);
	writebuf = (unsigned int*)pbuffer;

	for (i=0; i < alignsize; i++) {
		writel(writebuf[i], &sendmsg->data[i]);
	}

	if ( remainsize )
	{
		writel(writebuf[i]&(0xffffffff>>(4-remainsize)), &sendmsg->data[i]);
	}

	writel(size, &sendmsg->info);
	writel(MESSAGE_SEND, &sendmsg->pend);

	/* EDID workaround */
	/* wait micom ack (0x800590 = 1) */
	while(readl((void *)0xfe700590) != 1);
	
	/* skip EDID problem board */
	if (sdp_soc() == SDP1406UHD_CHIPID &&
		readl((void *)0xfee00000) & (1 << 16))
		goto out;

	/* EDID read start toggle reset [5]->1 */
	writel(readl((void *)0xfe480500) | (1 << 5), (void *)0xfe480500);

out:
	/* send to micom (0x800590 = 0) */
	writel(0, (void *)0xfe700590);
	/* end of EDID workaround */

	return size;
}
EXPORT_SYMBOL(sdp_messagebox_write_direct);



int sdp_messagebox_update(unsigned char* pbuffer, unsigned int size)
{
#if 0
	unsigned int *pimagebase;
	unsigned int *pcodebase;
	unsigned int alignsize;
	unsigned int remainsize;
	unsigned int i;
	unsigned int wait_count=3000;
	unsigned int retry_count=3;
	unsigned char * spi_mode = NULL;
	unsigned char * spi_erase = NULL;	
	unsigned char * spi_delay = NULL;
	unsigned char * reset = NULL;

	printk("updateee...\n");

	if (messagebox_base) {
		spi_mode = ((unsigned char*)messagebox_base+SPIMODE_OFFSET);
		spi_erase = ((unsigned char*)messagebox_base+SPIERASE_OFFSET);
		spi_delay = ((unsigned char*)messagebox_base+SPIDELAY_OFFSET);
		reset = ((unsigned char*)messagebox_base+SWRESET0_OFFSET);
	} else {
		printk(KERN_ERR "update failed : no base\n");
		return -1;
	}

	
	writel('S', &sendmsg->data[0]);
	writel('O', &sendmsg->data[1]);
	writel('C', &sendmsg->data[2]);	
	writel(0, &sendmsg->data[3]);
	
	writel(MESSAGE_UPDATE, &sendmsg->pend);	

	while(wait_count--) {
		if (UPDATE_READY_DONE == readl(&sendmsg->data[3])) {		
			break;
		}
		mdelay(5);
	}

	if(!wait_count) {
		printk(KERN_ERR "update failed : wait time over\n");
		return -1;
	}

	pimagebase = (unsigned int*)pbuffer;
	pcodebase = (unsigned int*)code_base;

	alignsize = size/4;
	remainsize = size%sizeof(unsigned int);		

	printk(KERN_INFO"start update...\n");

	do {		
		//micom contorol	
		writel(0x0, spi_erase); 
		writel(0x110, spi_mode);
		writel(0x2601040, spi_delay);
		writel(0x12000000, spi_erase);
		
		msleep(1000);
		
		for(i=0; i < alignsize;i++) {
			writel(cpu_to_be32(pimagebase[i]), &pcodebase[i]);
		}

		if (remainsize) {
			writel(cpu_to_be32(pimagebase[i])&(0xffffffff>>(4-remainsize)), &pcodebase[i]);
		}

		//Check Writed Data
		for(i=0; i < alignsize;i++) {
			if (cpu_to_be32(pimagebase[i]) != readl(&pcodebase[i]) ) {
					printk("CHECK FAILED.(%d)..\n", i);
					continue;
			}	
		}
		
		if ( remainsize ) {
			if ((cpu_to_be32(pimagebase[i])&(0xffffffff>>(4-remainsize))) != readl(&pcodebase[i])) {
					printk("CHECK FAILED LAST...\n");		
					continue;
			}	
		}
		
		printk(KERN_INFO"update end...\n");
		
		//micom reset	
		writel(0x100, spi_mode);
		writel(0x0, reset);
		msleep(1000);
		writel(0xc0000000, reset);	

		break;
		
	}while(retry_count--);
#endif
	return 0;
}
EXPORT_SYMBOL(sdp_messagebox_update);


int sdp_messagebox_installcallback(sdp_messagebox_callback_t callbackfn)
{
	read_callback = callbackfn;
	return 0;
}
EXPORT_SYMBOL(sdp_messagebox_installcallback);


static int sdp_messagebox_probe (struct platform_device *pdev)
{	
	struct device *dev = &pdev->dev;
	int irq;
	int ret;
	u32 cpu_aff;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not foound\n");
		return -ENXIO;
	}

	messagebox_base = of_iomap(dev->of_node, 0);
	BUG_ON(!messagebox_base);

#if 0
	code_base = of_iomap(dev->of_node, 1);
	BUG_ON(!code_base);
#endif

	recvmsg = (Msg_Box_t*)((unsigned char*)messagebox_base+RECVBOX_OFFSET);
	sendmsg = (Msg_Box_t*)((unsigned char*)messagebox_base+SENDBOX_OFFSET);	

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;
	}
		
	ret = request_threaded_irq( irq, NULL, messagebox_isr, IRQF_ONESHOT, pdev->name, (void*)dev);
	if (ret) {
		dev_err(dev, "request_irq failed\n");
		return -ENODEV;
	}

	
	if(of_property_read_u32(dev->of_node, "irq-affinity", &cpu_aff)==0) {
		if(num_online_cpus() > 1) { 	
			irq_set_affinity(irq, cpumask_of(cpu_aff));
		}
	} else {
		dev_info(dev, "cpu affinity get fail\n");
	}
		
	dev_info(dev, "sdp message driver\n");	
	return 0;
	
}


static int sdp_messagebox_remove(struct platform_device *pdev)
{
	return 0;		
}

static int sdp_messagebox_suspend(struct device *dev)
{
	return 0;
}

static int sdp_messagebox_resume(struct device *dev)
{
	return 0;
}

static const struct of_device_id sdp_messagebox_dt_match[] = {
	{ .compatible = "samsung,sdp-messagebox" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_messagebox_dt_match);

static struct dev_pm_ops sdp_messagebox_pm = {
	.suspend_noirq	= sdp_messagebox_suspend,
	.resume_noirq	= sdp_messagebox_resume,
};


static struct platform_driver sdp_messagebox_driver = {
	.probe		= sdp_messagebox_probe,
	.remove 	= sdp_messagebox_remove,
	.driver 	= {
		.name = "sdp-messagebox",
#ifdef CONFIG_OF
		.of_match_table = sdp_messagebox_dt_match,
#endif
		.pm	= &sdp_messagebox_pm,
	},
};


static int __init
sdp_messagebox_init (void)
{
	return platform_driver_register(&sdp_messagebox_driver);	
}

static void __exit sdp_messagebox_exit(void)
{
	platform_driver_unregister(&sdp_messagebox_driver);
}


arch_initcall(sdp_messagebox_init);
module_exit(sdp_messagebox_exit);


MODULE_DESCRIPTION("Samsung DTV messagebox driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("douk.nam <douk.nam@samsung.com>");


