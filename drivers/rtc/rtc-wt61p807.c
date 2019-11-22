#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
//#include <linux/mfd/sdp_micom.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/of.h>
#include <mach/soc.h>
#include <linux/bcd.h>

#define DRIVER_NAME		"rtc-wt61p807"

#define SDP_MICOM_DATA_LEN	5

#define WT61P807_REGISTER 0x00190500

#if 0
#define WT61P807_SEC	  	0x00190528
#define WT61P807_MIN  		0x0019052C
#define WT61P807_HOUR   	0x00190530

#define WT61P807_DATE   	0x00190534
#define WT61P807_DAY    	0x00190538
#define WT61P807_MONTH   	0x0019053C
#define WT61P807_YEAR   	0x00190540
#endif

#define WT61P807_SEC	  	0x28
#define WT61P807_MIN  		0x2C
#define WT61P807_HOUR   	0x30

#define WT61P807_DATE   	0x34
#define WT61P807_DAY    	0x38
#define WT61P807_MONTH   	0x3C
#define WT61P807_YEAR   	0x40

#define IS_LEAP_YEAR(year) (((year)%400)==0 || (((year)%100)!=0 && ((year)%4)==0))

#define rtc_readl(rtc, reg) \
	__raw_readl((rtc)->base + (reg))
#define rtc_writel(rtc, reg, value) \
	__raw_writel((value), (rtc)->base + (reg))

struct wt61p807_rtc {
	struct resource *ress;
	void __iomem 	*base;
	struct rtc_device *rtc_dev;
	struct rtc_time *rtc_tm;
	spinlock_t 		lock;
};

typedef struct {
	u32   second:       6; /* XXX */
	u32   resv0:        2; /* XXX */
	u32   minute:       6; /* XXX */
	u32   resv1:        2; /* XXX */
	u32   hour:         5; /* XXX */
	u32   resv2:        8; /* XXX */
	u32   wk_no:        3; /* XXX */
}rtc_hh_mm_ss;

/*
static void wt61p807_rtc_gettime_calc(char *data, struct rtc_time *rtc_tm)
{
	if (data) {
		rtc_tm->tm_year = ((data[1] & 0xFE) >> 1);
		rtc_tm->tm_mon = ((data[1] & 0x01) << 3) |
				((data[2] & 0xE0) >> 5);
		rtc_tm->tm_mday = (data[2] & 0x1F);
		rtc_tm->tm_hour = (data[3] & 0x1F);
		rtc_tm->tm_min = (data[4] & 0x3F);
		rtc_tm->tm_sec = (data[5] & 0x3F);

		rtc_tm->tm_year += 100;
		rtc_tm->tm_mon -= 1;
	} else {
		rtc_tm->tm_year = 0;
		rtc_tm->tm_mon = 0;
		rtc_tm->tm_mday = 0;
		rtc_tm->tm_hour = 0;
		rtc_tm->tm_min = 0;
		rtc_tm->tm_sec = 0;
	}
}
*/

/*
static void wt61p807_rtc_gettime_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	struct wt61p807_rtc *rtc = dev_id;

	if (!rtc)
		return;

	wt61p807_rtc_gettime_calc(msg->msg, rtc->rtc_tm);
}
*/

static int wt61p807_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct wt61p807_rtc *rtc = dev_get_drvdata(dev);
	int actual_year = 0;
	int line = 0;
	
	dev_dbg(dev, "%s(%d): time to set %d-%d-%d %d:%d:%d\n", __func__, __LINE__, tm->tm_year + 1900,
		tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		
	actual_year = tm->tm_year + 1900;
	if(actual_year > 2100 || actual_year < 1970) {
		dev_err(dev, "%s(%d) err: rtc only supports 130(1970~2100) years\n", __func__, __LINE__);
		return -EINVAL;
	}
	
	tm->tm_mon  += 1;
	switch(tm->tm_mon) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		if(tm->tm_mday > 31)
			line = __LINE__;
		if(tm->tm_hour > 24 || tm->tm_min > 59 || tm->tm_sec > 59)
			line = __LINE__;
		break;
	case 4:
	case 6:
	case 9:
	case 11:
		if(tm->tm_mday > 30)
			line = __LINE__;
		if(tm->tm_hour > 24 || tm->tm_min > 59 || tm->tm_sec > 59)
			line = __LINE__;
		break;
	case 2:
		if(IS_LEAP_YEAR(actual_year)) {
			if(tm->tm_mday > 29)
				line = __LINE__;
			if(tm->tm_hour > 24 || tm->tm_min > 59 || tm->tm_sec > 59)
				line = __LINE__;
		} else {
			if(tm->tm_mday > 28)
				line = __LINE__;
			if(tm->tm_hour > 24 || tm->tm_min > 59 || tm->tm_sec > 59)
				line = __LINE__;
		}
		break;
	default:
		line = __LINE__;
		break;
	}
	
	if(0 != line) {
		dev_err(dev, "%s(%d) err: date %d-%d-%d %d:%d:%d, so reset to 1900-1-1 00:00:00\n", __func__, line,
			tm->tm_year + 1900, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		tm->tm_sec  = 0;
		tm->tm_min  = 0;
		tm->tm_hour = 0;
		tm->tm_mday = 0;
		tm->tm_mon  = 0;
		tm->tm_year = 0;
	}
	
	dev_dbg(dev, "%s(%d): actually set time to %d-%d-%d %d:%d:%d\n", __func__, __LINE__, tm->tm_year + 1900,
		tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc_writel(rtc, WT61P807_SEC, bin2bcd(tm->tm_sec));
	rtc_writel(rtc, WT61P807_MIN, bin2bcd(tm->tm_min));
	rtc_writel(rtc, WT61P807_HOUR, bin2bcd(tm->tm_hour));

	rtc_writel(rtc, WT61P807_DAY, bin2bcd(tm->tm_mday));
	rtc_writel(rtc, WT61P807_MONTH, bin2bcd(tm->tm_mon));
	rtc_writel(rtc, WT61P807_YEAR, bin2bcd(tm->tm_year));

//	writel(bin2bcd(tm->tm_sec), WT61P807_SEC);
//	writel(bin2bcd(tm->tm_min), WT61P807_MIN);
//	writel(bin2bcd(tm->tm_hour), WT61P807_HOUR);
	
//	writel(bin2bcd(tm->tm_mday), WT61P807_DAY);
//	writel(bin2bcd(tm->tm_mon), WT61P807_MONTH);
//	writel(bin2bcd(tm->tm_year), WT61P807_YEAR);
	
	return 0;
	
/*
	char data[SDP_MICOM_DATA_LEN];
	int year;
	
	if (get_sdp_board_type() == SDP_BOARD_SBB) {
		return -EINVAL;	
	}
	
	year = tm->tm_year - 100;

	if (year < 0 || year >= 128) {
		dev_err(dev, "[RTC]rtc only supports 127 years(year:%d)\n", tm->tm_year);
		return -EINVAL;
	}

	data[0] = (year << 1) | (((tm->tm_mon + 1) & 0x08) >> 3);
	data[1] = (((tm->tm_mon + 1) & 0x07) << 5) | tm->tm_mday;
	data[2] = tm->tm_hour;
	data[3] = tm->tm_min;
	data[4] = tm->tm_sec;
	printk("[RTC]Set Current Time[%04d(%d):%02d:%02d %02d:%02d:%02d]\n", tm->tm_year, year, tm->tm_mon, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

	sdp_micom_send_cmd_sync(SDP_MICOM_CMD_SET_TIME, SDP_MICOM_ACK_SET_TIME,
		data, SDP_MICOM_DATA_LEN);

	return 0;
*/
}

static int wt61p807_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wt61p807_rtc *rtc = platform_get_drvdata(pdev);
	//struct rtc_time *rtc_micom = NULL;
	
	rtc_tm->tm_sec = bcd2bin(rtc_readl(rtc, WT61P807_SEC));
	rtc_tm->tm_min = bcd2bin(rtc_readl(rtc, WT61P807_MIN));
	rtc_tm->tm_hour = bcd2bin(rtc_readl(rtc, WT61P807_HOUR));
	
	rtc_tm->tm_mday = bcd2bin(rtc_readl(rtc, WT61P807_DAY));
	rtc_tm->tm_mon = bcd2bin(rtc_readl(rtc, WT61P807_DATE));
	rtc_tm->tm_year = bcd2bin(rtc_readl(rtc, WT61P807_YEAR));
	
	dev_dbg(dev, "%s(%d): rtc_tm->tm_year:%d\n", __func__, __LINE__, rtc_tm->tm_year);
	if (rtc_tm->tm_year < 70) {
		dev_dbg(dev, "%s(%d): year < 70, so add 100, to check\n", __func__, __LINE__);
		//rtc_tm->tm_year += 100;	/* assume we are in 1970...2069 */
		rtc_tm->tm_year += 114; 	//hsguy.son (modify)	/* assume we are in 1970...2069 */
	}
	
	rtc_tm->tm_mon -= 1;
	dev_dbg(dev, "%s(%d): read time %d-%d-%d %d:%d:%d\n", __func__, __LINE__, rtc_tm->tm_year + 1900,
		rtc_tm->tm_mon + 1, rtc_tm->tm_mday, rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
	return rtc_valid_tm(rtc_tm);
/*
	if (!pdev || !rtc || !rtc_tm || (get_sdp_board_type() == SDP_BOARD_SBB)) {
		return -EINVAL;
	}

	sdp_micom_send_cmd_sync(SDP_MICOM_CMD_GET_TIME, SDP_MICOM_ACK_GET_TIME, NULL, 0);

	rtc_micom = rtc->rtc_tm;
	printk("[RTC]Get Current Time[%04d:%02d:%02d %02d:%02d:%02d]\n", rtc_micom->tm_year, rtc_micom->tm_mon, rtc_micom->tm_mday,
			rtc_micom->tm_hour, rtc_micom->tm_min, rtc_micom->tm_sec);

	if (rtc_valid_tm(rtc_micom) < 0) {
		
		printk("[RTC]Invalid time.!!\n");
		rtc_micom->tm_year     = 100;	//Base Year : 2000year
		rtc_micom->tm_mon      = 0;
		rtc_micom->tm_mday     = 1;
		rtc_micom->tm_hour     = 0;
		rtc_micom->tm_min      = 0;
		rtc_micom->tm_sec      = 0;

		wt61p807_rtc_settime(&pdev->dev, rtc_micom);
	}

	memcpy(rtc_tm, rtc_micom, sizeof(struct rtc_time));

	return rtc_valid_tm(rtc_tm);
*/
}

static const struct rtc_class_ops wt61p807_rtcops = {
	.read_time	= wt61p807_rtc_gettime,
	.set_time	= wt61p807_rtc_settime,
};

static int wt61p807_rtc_probe(struct platform_device *pdev)
{
	struct wt61p807_rtc *rtc;
	struct rtc_time *rtc_tm;
	//struct sdp_micom_cb *micom_cb;
	int ret;

	rtc = kzalloc(sizeof(struct wt61p807_rtc), GFP_KERNEL);
	if(!rtc)
		return -ENOMEM;
	
	rtc_tm = devm_kzalloc(&pdev->dev, sizeof(struct rtc_time), GFP_KERNEL);
	if (!rtc_tm) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	rtc->rtc_tm = rtc_tm;

	//device_init_wakeup(&pdev->dev, 1);
	
	platform_set_drvdata(pdev, rtc);
	ret = -ENXIO;

	rtc->ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(!rtc->ress)
	{
		dev_err(&pdev->dev, "No I/O memory resource defined\n");
		goto err_ress;
	}

	ret = -ENOMEM;
	rtc->base = ioremap(rtc->ress->start, resource_size(rtc->ress));
	if(!rtc->base)
	{
		dev_err(&pdev->dev, "Unable to map RTC I/O memory\n");
		goto err_map;
	}

	/* register rtc device */
	rtc->rtc_dev = rtc_device_register("rtc", &pdev->dev, &wt61p807_rtcops, THIS_MODULE);
	if(IS_ERR(rtc->rtc_dev)) {
		dev_err(&pdev->dev, "err: cannot attach rtc\n");
		ret = PTR_ERR(rtc->rtc_dev);
		goto err_rtc_reg;
	}

	device_init_wakeup(&pdev->dev, 1);

	return 0;

err_rtc_reg:
	iounmap(rtc->base);
err_ress:
err_map:
	kfree(rtc);
	return ret;
/*
	rtc = devm_kzalloc(&pdev->dev, sizeof(struct wt61p807_rtc), GFP_KERNEL);
	if (!rtc) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	rtc_tm = devm_kzalloc(&pdev->dev, sizeof(struct rtc_time), GFP_KERNEL);
	if (!rtc_tm) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	rtc->rtc_tm = rtc_tm;
	platform_set_drvdata(pdev, rtc);

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);
	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	micom_cb->id		= SDP_MICOM_DEV_RTC;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_rtc_gettime_cb;
	micom_cb->dev_id	= rtc;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		return ret;
	}

	rtc->rtc_dev = rtc_device_register("wt61p807", &pdev->dev,
				&wt61p807_rtcops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(rtc->rtc_dev);
		return ret;
	}

	return 0;
	*/
}

static int wt61p807_rtc_remove(struct platform_device *pdev)
{
	struct wt61p807_rtc *rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc->rtc_dev);

	spin_lock_irq(&rtc->lock);
	iounmap(rtc->base);
	spin_unlock_irq(&rtc->lock);

	kfree(rtc);

	return 0;
}

/*
static struct resource wt61p807_rtc_resource[] = {
	[0] = {
		.start = SW_INT_IRQNO_ALARM,
		.end   = SW_INT_IRQNO_ALARM,
		.flags = IORESOURCE_IRQ,
	},
};
*/

static const struct of_device_id rtc_wt61p807_dt_match[] = {
	{ .compatible = "samsung,rtc-wt61p807", },
	{},
}
MODULE_DEVICE_TABLE(of, rtc_wt61p807_dt_match);

struct platform_device wt61p807_device_rtc = {
	.name		= "wt61p807-rtc",
	.id		= -1,
	//.num_resources	= ARRAY_SIZE(wt61p807_rtc_resource),
	//.resource	= wt61p807_rtc_resource,
};

static struct platform_driver wt61p807_rtc_driver = {
	.probe		= wt61p807_rtc_probe,
	.remove		= wt61p807_rtc_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rtc_wt61p807_dt_match),
	},
};

static int wt61p807_init(void)
{
	platform_device_register(&wt61p807_device_rtc);
	return platform_driver_register(&wt61p807_rtc_driver);
}

static void __exit wt61p807_exit(void)
{	
	platform_driver_unregister(&wt61p807_rtc_driver);
}

module_init(wt61p807_init);
module_exit(wt61p807_exit);

module_platform_driver(wt61p807_rtc_driver);
