/*
 *  linux/kernel/sec_netusage.c
 *
 *  Network Performance Profiling Solution, network usage releated functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-05-29  Created by Choi Young-Ho, Kim Geon-Ho
 *
 */
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/netdevice.h>

#include <linux/proc_fs.h>
#include <kdebugd.h>
#include <sec_netusage.h>
#include "kdbg_util.h"

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
#include "agent/agent_core.h"
#include "agent/agent_packet.h"
#include "agent/agent_error.h"
#include "agent/tvis_agent_cmds.h"

/* CPU usage ERROR */
#define netusage_error(fmt, args...) \
	kdbg_error(KDBG_CMD_CM_NET_USAGE, fmt, ##args)

/* CPU usage output data */
#define netusage_write(data, len) \
	agent_write(KDBG_CMD_CM_NET_USAGE, data, len)

#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

/*
 * Turn ON the net usage processing.
 */
static int sec_netusage_init(void);

/*
 * The buffer that will store net usage data.
 */
static struct sec_netusage_struct *g_psec_netusage_buffer_;	/*[SEC_NETUSAGE_BUFFER_ENTRIES]; */
DEFINE_MUTEX(netbuffer_lock);

/*Init flag */
static int sec_netusage_init_flag;

/* check whether state is running or not..*/
static int sec_netusage_run_state;

/*The index of the buffer array at which the data will be written.*/
static int sec_netusage_write_index;

/*The flag which incates whether the buffer array is full(value is 1)
 *or is partially full(value is 0).
 */
static int sec_netusage_is_buffer_full;

/*The flag which will be turned on or off when sysrq feature will
 *turn on or off respectively.
 */

/*Network device*/
static struct net_device *dev;

/* Flag is used to turn the sysrq for net usage on or off */
static int sec_netusage_status;

static int sec_netusage_update_flag;

/* This function is to show header */
static void netusage_show_header(void)
{
	PRINT_KD("\ntime(1 Sec)\tRx(Bytes)\tTx(Bytes)\n");
	PRINT_KD("============\t===========\t===========\n");
}

/*Dump the bufferd data of net usage from the buffer.
  This Function is called from the kdebug menu. It prints the data
  same as printed by cat /proc/netusage-gnuplot*/
void sec_netusage_gnuplot_dump(void)
{
	int last_row = 0, saved_row = 0;
	int idx, i;
	int limit_count;

	if (sec_netusage_init_flag) {

		BUG_ON(!g_psec_netusage_buffer_);

		if (sec_netusage_is_buffer_full) {
			limit_count = SEC_NETUSAGE_BUFFER_ENTRIES;
			last_row = sec_netusage_write_index;
		} else {
			limit_count = sec_netusage_write_index;
			last_row = 0;
		}
		saved_row = last_row;

		PRINT_KD("\n\n\n");
		PRINT_KD("{{{#!gnuplot\n");
		PRINT_KD("reset\n");
		PRINT_KD("set title \"Network Usage\"\n");
		PRINT_KD("set xlabel \"time(sec)\"\n");
		PRINT_KD("set ylabel \"Usage(Bytes)\"\n");
		PRINT_KD("set key autotitle columnheader\n");
		PRINT_KD("set auto x\n");
		PRINT_KD("set xtics nomirror rotate by 90\n");
		PRINT_KD("set grid ytics\n");
		PRINT_KD("set lmargin 10\n");
		PRINT_KD("set rmargin 1\n");
		PRINT_KD("#\n");
		PRINT_KD
		    ("plot \"-\" using 2:xtic(1) with lines, '' using 3 with lines\n");

		/* Becuase of gnuplot grammar, we should print data twice
		 * to draw the two graphic lines on the chart.
		 */
		for (i = 0; i < 2; i++) {
			PRINT_KD("Sec\t\tRx\tTx\n");
			for (idx = 0; idx < limit_count; idx++) {
				int index =
				    last_row % SEC_NETUSAGE_BUFFER_ENTRIES;
				last_row++;
				PRINT_KD("%04ld\t\t%u\t%u\n",
					 g_psec_netusage_buffer_[index].sec,
					 g_psec_netusage_buffer_[index].rx,
					 g_psec_netusage_buffer_[index].tx);
			}
			PRINT_KD("e\n");
			last_row = saved_row;
		}

		PRINT_KD("}}}\n");
	}
}

/*Dump the bufferd data of net usage from the buffer.
 *This Function is called from the kdebug menu.
 */
void sec_netusage_dump(void)
{
	int i = 0;
	int buffer_count = 0;
	int idx = 0;

	if (sec_netusage_init_flag) {

		BUG_ON(!g_psec_netusage_buffer_);
		if (sec_netusage_is_buffer_full) {
			buffer_count = SEC_NETUSAGE_BUFFER_ENTRIES;
			idx = sec_netusage_write_index;
		} else {
			buffer_count = sec_netusage_write_index;
			idx = 0;
		}

		netusage_show_header();

		mutex_lock (&netbuffer_lock);
		if (g_psec_netusage_buffer_) {

			for (i = 0; i < buffer_count; ++i, ++idx) {
				idx = idx % SEC_NETUSAGE_BUFFER_ENTRIES;

				PRINT_KD("%04ld Sec\t%u\t\t%u\n",
						g_psec_netusage_buffer_[idx].sec,
						g_psec_netusage_buffer_[idx].rx,
						g_psec_netusage_buffer_[idx].tx);
			}
		}
		mutex_unlock (&netbuffer_lock);
	}
}

static void show_net_stat(struct net_device *net_dev)
{
	static unsigned long old_rx_bytes = 0, old_tx_bytes;
	unsigned long rx_persec, tx_persec;

	if (sec_netusage_init_flag) {

		/* Chack whether back ground runing is on or not */
		if (!sec_netusage_run_state)
			return;

		BUG_ON(!g_psec_netusage_buffer_);

		if (net_dev->netdev_ops->ndo_get_stats) {
			struct net_device_stats *stats =
			    net_dev->netdev_ops->ndo_get_stats(net_dev);

			rx_persec = (stats->rx_bytes - old_rx_bytes);
			tx_persec = (stats->tx_bytes - old_tx_bytes);

			old_rx_bytes = stats->rx_bytes;
			old_tx_bytes = stats->tx_bytes;

			if (sec_netusage_update_flag) {

				if (sec_netusage_status) {
					PRINT_KD
					    ("+--------------------------------------------+\n");
					PRINT_KD
					    ("|   Receive (Bytes)   |   Transmit (Bytes)   |\n");
					PRINT_KD
					    ("|   %8lu          |   %8lu           |\n",
					     rx_persec, tx_persec);
					PRINT_KD
					    ("+--------------------------------------------+\n");
				}

				mutex_lock (&netbuffer_lock);
				if (g_psec_netusage_buffer_) {
				g_psec_netusage_buffer_
				    [sec_netusage_write_index].sec =
				    kdbg_get_uptime();
				g_psec_netusage_buffer_
				    [sec_netusage_write_index].rx = (int)rx_persec;
				g_psec_netusage_buffer_
				    [sec_netusage_write_index].tx = (int)tx_persec;
				}
				mutex_unlock (&netbuffer_lock);

				++sec_netusage_write_index;

				if (sec_netusage_write_index >=
				    SEC_NETUSAGE_BUFFER_ENTRIES) {
					sec_netusage_is_buffer_full = 1;
					sec_netusage_write_index = 0;
				}
			} else {
				sec_netusage_update_flag = 1;
			}

		} else if (sec_netusage_status) {
			PRINT_KD("No statistics available.\n");
		}
	}
}

static int net_show_func(void)
{
	show_net_stat(dev);
	return 0;
}

/* Check the device is ethernet or not ?
 * This fucntion is used for identify ethernet device name.
 * Currently compare eth* name.
 */
static inline int is_ethernet (struct net_device *netdev)
{
	if (!netdev)
		return -1;
	if (netdev->name[0] == 'e' &&
			netdev->name[1] == 't' &&
			netdev->name[2] == 'h')
		return 0;
	else
		return -1;
}

/* Initialize network device */
static int sec_netdevice_init(void)
{
	/*Find the network device*/
	dev = first_net_device_rcu(&init_net);
	if (!dev) {
		PRINT_KD(KERN_ERR "Ethernet device doesn't exits, aborting.\n");
		return false;
	}

	/* Loopback device, leave it*/
	while (dev &&  (!strcmp (dev->name, "lo") || is_ethernet(dev)))
		dev = next_net_device_rcu(dev);

	/* Take first ethernet device only */
	if (!dev) {
		PRINT_KD(KERN_ERR "Ethernet device doesn't exits, aborting.\n");
		return false;
	}
	PRINT_KD ("Ethernet device name %s\n", dev->name);
	return true;
}

int sec_netusage_init(void)
{
	/* Initialize network device */
	if (!sec_netdevice_init())
		return false;

	/* Turn ON the processing of dumping the mem usage data. */
	g_psec_netusage_buffer_ = (struct sec_netusage_struct *)
	    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_KDEBUGD_MODULE,
				 SEC_NETUSAGE_BUFFER_ENTRIES *
				 sizeof(struct sec_netusage_struct),
				 GFP_ATOMIC);

	if (!g_psec_netusage_buffer_) {
		PRINT_KD("NETWORK USAGE ERROR: Insuffisient memory\n");
		sec_netusage_init_flag = 0;
		return false;
	}

	memset(g_psec_netusage_buffer_, 0, SEC_NETUSAGE_BUFFER_ENTRIES *
	       sizeof(struct sec_netusage_struct));

	if (register_counter_monitor_func (net_show_func) < 0) {
		PRINT_KD("WARN: Fail to Register Counter Monitor function\n");
		if (g_psec_netusage_buffer_) {
			KDBG_MEM_DBG_KFREE(g_psec_netusage_buffer_);
			g_psec_netusage_buffer_ = NULL;
		}
	}
	sec_netusage_write_index = 0;
	sec_netusage_is_buffer_full = 0;
	sec_netusage_init_flag = 1;
	sec_netusage_update_flag = 0;
	return true;
}

/* This is called from kdbg_agent_worker thread */
#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
void sec_netusage_agent_interrupt(void)
{
	size_t ret = 0;
	struct sec_netusage_struct netusage;

	/* Initialize the network device */
	if (!dev) {
		if (!sec_netdevice_init())
			return;
	}

	if (dev->netdev_ops->ndo_get_stats) {
		struct net_device_stats *stats =
			dev->netdev_ops->ndo_get_stats(dev);

		netusage.sec = kdbg_get_uptime();
		netusage.rx = (int)stats->rx_bytes;
		netusage.tx = (int)stats->tx_bytes;

	}

	ret = netusage_write(&netusage, sizeof(netusage));
	if (!ret)
		netusage_error("Error in writing to RB.\n");
}
#endif /* CONFIG_KDEBUGD_AGENT_SUPPORT */

void get_netusage_status(void)
{
	if (sec_netusage_init_flag) {

		PRINT_KD("Initialized        ");
		if (sec_netusage_run_state)
			PRINT_KD("Running\n");
		else
			PRINT_KD("Not Running\n");
	} else {
		PRINT_KD("Not Initialized    Not Running\n");
	}
}

void sec_netusage_destroy(void)
{
	sec_netusage_status = 0;
	sec_netusage_run_state = 0;

	if (unregister_counter_monitor_func (net_show_func) < 0) {
		PRINT_KD("WARN: Fail to Unregister Counter Monitor function\n");
	}

	if (g_psec_netusage_buffer_) {

		mutex_lock (&netbuffer_lock);
		KDBG_MEM_DBG_KFREE(g_psec_netusage_buffer_);
		g_psec_netusage_buffer_ = NULL;
		mutex_unlock (&netbuffer_lock);

		sec_netusage_init_flag = 0;
		PRINT_KD("NETUSAGE Destroyed Successfuly\n");
	} else {
		PRINT_KD("Already Not Initialized\n");
	}
}

/*
 *Turn off the prints of netusage
 */
static void turnoff_netusage(void)
{
	if (sec_netusage_status) {
		sec_netusage_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Network USAGE Dump OFF\n");
	}

}

/*
 *Turn the prints of netusage on
 *or off depending on the previous status.
 */
void sec_netusage_prints_OnOff(void)
{
	sec_netusage_status = (sec_netusage_status) ? 0 : 1;

	if (sec_netusage_status)
		PRINT_KD("Network USAGE Dump ON\n");
	else
		PRINT_KD("Network USAGE Dump OFF\n");
}

static int sec_netusage_control(void)
{
	int operation = 0;
	int ret = 1;

	if (!sec_netusage_init_flag) {
		if (sec_netusage_init() == false) {
			return -1;
		}
	}

	PRINT_KD("\n");
	PRINT_KD("Select Operation....\n");
	PRINT_KD("1. Turn On/Off the Network Usage prints\n");
	PRINT_KD("2. Dump Network Usage history(%d sec)\n",
		 SEC_NETUSAGE_BUFFER_ENTRIES);
	PRINT_KD("3. Dump Network Usage gnuplot history(%d sec)\n",
		 SEC_NETUSAGE_BUFFER_ENTRIES);
	PRINT_KD("==>  ");

	operation = debugd_get_event_as_numeric(NULL, NULL);

	PRINT_KD("\n\n");

	switch (operation) {
	case 1:
		sec_netusage_run_state = 1;
		sec_netusage_prints_OnOff();
		ret = 0;	/* don't print the menu */
		break;
	case 2:
		sec_netusage_dump();
		break;
	case 3:
		sec_netusage_gnuplot_dump();
		break;
	default:
		break;
	}
	return ret;
}

void sec_netusage_OnOff(void)
{
	if (sec_netusage_init_flag) {
		sec_netusage_destroy();
	} else {
		if (sec_netusage_init() == false) {
			return;
		}
		/* start the mem usage after init */
		sec_netusage_run_state = 1;
	}
}

#if defined(CONFIG_SEC_NETUSAGE_AUTO_START) && defined(CONFIG_COUNTER_MON_AUTO_START_PERIOD)
/* The feature is for auto start cpu usage for taking the log
 * of the secified time */
static struct timer_list netusage_auto_timer;
/* Auto start loging the cpu usage data */
static void netusage_auto_start(unsigned long duration)
{
	static int started;

	BUG_ON(started != 0 && started != 1);

	if (!sec_netusage_init_flag) {
		PRINT_KD("Error: Net Usage Not Initialized\n");
		return;
	}

	/* Make the status running */
	if (!started) {

		sec_netusage_run_state = 1;

		/* timer setup for stop */
		mod_timer(&netusage_auto_timer, duration);
		started = 1;

	} else {

		if (!sec_netusage_status)
			sec_netusage_run_state = 0;

		started = 0;
		del_timer(&netusage_auto_timer);
	}
	return;
}
#endif

int kdbg_netusage_init(void)
{
	sec_netusage_init_flag = 0;

#ifdef CONFIG_SEC_NETUSAGE_AUTO_START
	if (sec_netusage_init() == false) {
		return -1;
	}
#ifdef CONFIG_COUNTER_MON_AUTO_START_PERIOD
	/* setup your timer to call netusage_auto_timer_callback */
	setup_timer(&netusage_auto_timer, netusage_auto_start, jiffies + msecs_to_jiffies(CONFIG_COUNTER_MON_FINISHED_SEC * 1000));
	netusage_auto_timer.expires = jiffies + msecs_to_jiffies(CONFIG_COUNTER_MON_START_SEC * 1000);
	/* setup timer interval to 200 msecs */
	add_timer(&netusage_auto_timer);
#else
	sec_netusage_run_state = 1;
#endif

#endif

	kdbg_register("COUNTER MONITOR: Network Usage", sec_netusage_control,
		      turnoff_netusage,
		      KDBG_MENU_COUNTER_MONITOR_NETWORK_USAGE);

	return 0;
}
