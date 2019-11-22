/*
 * Dummy RTC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/time.h>
#define FALSE 0
#define TRUE  1
#define DRIVER_NAME	 "DUMMY_RTC"
#define SDP_MICOM_DATA_LEN 5

#define rtc_dbgmsg(str, args...) pr_info("%s: "str, __func__, ##args)

static bool IS_TIME_INIT;
static unsigned long itime;

static unsigned long boot_time;
static void get_rtc_time_cb(struct sdp_micom_msg *msg, void *dev_id){
	
	//struct rtc_time *time = dev_id;
	struct rtc_time rtc_t;
	struct rtc_time * rtc_tm = &rtc_t;

	rtc_time_to_tm(0, &rtc_t);

	if (msg) {
		
		//rtc_tm->tm_year += (((msg->msg[1] & 0xFE) >> 1)- 140);
		rtc_tm->tm_year = (((msg->msg[1] & 0xFE) >> 1) + 100);
		rtc_tm->tm_mon = ((((msg->msg[1] & 0x01) << 3) |
			((msg->msg[2] & 0xE0) >> 5)) - 1);
		rtc_tm->tm_mday = (msg->msg[2] & 0x1F);
		rtc_tm->tm_hour = (msg->msg[3] & 0x1F);
		rtc_tm->tm_min = (msg->msg[4] & 0x3F);
		rtc_tm->tm_sec = (msg->msg[5] & 0x3F);
		rtc_tm_to_time(rtc_tm, &itime);
		boot_time = (get_seconds());

		rtc_dbgmsg(" Micom time info => %04d/%02d/%02d %02d:%02d:%02d \n", rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday, rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
		rtc_dbgmsg (" boot time : %ld \n", boot_time);
		itime = itime - boot_time;

		rtc_time_to_tm(itime,rtc_tm);
		rtc_dbgmsg(" AP time set => %04d/%02d/%02d %02d:%02d:%02d \n", rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday, rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
	} 	

	else {
		// micom cb failed ...
		rtc_dbgmsg(" Get Micom time failed ... \n");
		rtc_tm->tm_year += 46;
		rtc_tm->tm_hour += 9;
		rtc_dbgmsg(" AP time set => %04d/%02d/%02d %02d:%02d:%02d \n", rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday, rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
		rtc_time_to_tm(itime,rtc_tm);

	}
}

void micom_rtc_settime(void)
{
	char data[SDP_MICOM_DATA_LEN];
	struct rtc_time rtc_tm;
	struct rtc_time *tm = &rtc_tm;	
	struct timeval k_time;
	int year;
	do_gettimeofday(&k_time);
	rtc_time_to_tm(k_time.tv_sec, tm);
	rtc_dbgmsg("[KERNEL Time] %04d:%02d:%02d %02d:%02d:%02d]\n", tm->tm_year,tm->tm_mon, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	year = tm->tm_year - 100;
	data[0] = (year << 1) | (((tm->tm_mon + 1) & 0x08) >> 3);
	data[1] = (((tm->tm_mon + 1) & 0x07) << 5) | (tm->tm_mday);
	data[2] = tm->tm_hour;
	data[3] = tm->tm_min;
	data[4] = tm->tm_sec;

	sdp_micom_send_cmd(SDP_MICOM_CMD_SET_TIME, data, SDP_MICOM_DATA_LEN);

}
EXPORT_SYMBOL(micom_rtc_settime);


static int dummy_set_time(struct device *dev, struct rtc_time *time)
{
	if(IS_TIME_INIT){
		sdp_micom_send_cmd_sync(SDP_MICOM_CMD_GET_TIME, SDP_MICOM_ACK_GET_TIME, NULL, 0);
		IS_TIME_INIT = FALSE;
	}
	
	return 0;
}


static int dummy_read_time(struct device *dev, struct rtc_time *time)
{
	int ret = 0;
	rtc_time_to_tm((itime + (get_seconds())), time);
	return ret;
}

static int dummy_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	return 0;
}

static int dummy_set_alarm(struct device *dev, struct rtc_wkalrm *alarm){

	return 0;
}

static int dummy_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	return 0;
}
static const struct rtc_class_ops dummy_rtc_ops = {
	.read_time = dummy_read_time,
	.set_time = dummy_set_time,
	.read_alarm = dummy_read_alarm,
	.set_alarm = dummy_set_alarm,
	.alarm_irq_enable = dummy_alarm_irq_enable,
};

static int dummy_rtc_probe(struct platform_device *pdev)
{

	struct rtc_time time;
//	unsigned long itime;
	struct rtc_device *rtc;
	struct sdp_micom_cb *micom_cb;
	int ret;

	IS_TIME_INIT = TRUE;
	rtc = devm_rtc_device_register(&pdev->dev, "dummy", &dummy_rtc_ops, THIS_MODULE);
	
	if(IS_ERR(rtc)){
		dev_err(&pdev->dev, "can't register RTC device. \n");
		return PTR_ERR(rtc);

	}

	micom_cb = devm_kzalloc(&pdev->dev, sizeof(struct sdp_micom_cb),GFP_KERNEL);

	if(!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data \n");
	}

	micom_cb->id		= SDP_MICOM_DEV_RTC;
	micom_cb->name 		= DRIVER_NAME;
	micom_cb->cb		= get_rtc_time_cb;
	micom_cb->dev_id	= rtc;

	ret = sdp_micom_register_cb(micom_cb);
	if(ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed! \n");
	}

	rtc_time_to_tm(0,&time);
	dummy_set_time(&pdev->dev, &time);

	platform_set_drvdata(pdev,rtc);
	return 0;
}

static int dummy_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device * rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev,NULL);

	return 0;

}
static const struct of_device_id dummy_rtc_dt_match[] = {
	{ .compatible = "samsung,dummy-rtc",},
	{ },
};

MODULE_DEVICE_TABLE(of, dummy_rtc_dt_match);

static struct platform_driver dummy_rtc_driver = {
	.probe	= dummy_rtc_probe,
	.remove = dummy_rtc_remove,
	.driver = {
		.name	 = DRIVER_NAME,
		.owner	 = THIS_MODULE,
		.of_match_table = of_match_ptr(dummy_rtc_dt_match),
	},
};

static int __init dummy_rtc_init(void)
{
	return platform_driver_register(&dummy_rtc_driver);
}

static void __exit dummy_rtc_exit(void)
{
	platform_driver_unregister(&dummy_rtc_driver);
}

module_init(dummy_rtc_init);
module_exit(dummy_rtc_exit);

MODULE_AUTHOR("Scott Wood <scottwood@freescale.com>");
MODULE_DESCRIPTION("V7 dummy rtc");
MODULE_LICENSE("GPL");

