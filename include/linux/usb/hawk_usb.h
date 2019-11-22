/*
 * phy-hawk.c - USB PHY for USB3.0 controller in HAWK platform.
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * Author: Ikjoon Jang <ij.jang@samsung.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __HAWK_USB_PHY_H_
#define __HAWK_USB_PHY_H_

#include <linux/io.h>
#include <linux/atomic.h>
#include <linux/usb/otg.h>
#include <linux/usb/otg.h>

struct hawk_usb3_phy {
	struct usb_phy		phy;
	atomic_t		enable_count;
	atomic_t		reset_state;
	void __iomem		*gpr0_regs;
	void __iomem		*gpr1_regs;
	void __iomem		*phy_regs;
	struct clk		*usb2_clk;
	struct clk		*usb3_clk;
};

#endif

