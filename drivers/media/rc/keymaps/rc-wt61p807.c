/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table wt61p807[] = {
	{ 0x00, KEY_F17 },		/* mts */
	{ 0x01, KEY_F6 },		/* input mode */
	{ 0x02, KEY_POWER },
	{ 0x03, KEY_SLEEP },
	{ 0x04, KEY_1 },
	{ 0x05, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x07, KEY_F10 },		/* volume up */
	{ 0x08, KEY_4 },
	{ 0x09, KEY_5 },
	{ 0x0A, KEY_6 },
	{ 0x0B, KEY_F9 },		/* volume down */
	{ 0x0C, KEY_7 },
	{ 0x0D, KEY_8 },
	{ 0x0E, KEY_9 },
	{ 0x0F, KEY_F8 },		/* mute */
	{ 0x10, KEY_F11 },		/* channel down */
	{ 0x11, KEY_0 },
	{ 0x12, KEY_F12 },		/* channel up */
	{ 0x13, KEY_REDO },		/* previous channel */
	{ 0x14, KEY_F2 },		/* B (green) */
	{ 0x15, KEY_F3 },		/* C (yellow) */
	{ 0x16, KEY_F4 },		/* D (blue) */
	{ 0x19, KEY_PROG1 },		/* channel add/del */
	{ 0x1A, KEY_LEFTMETA },		/* menu */
	{ 0x1B, KEY_SENDFILE },		/* TV key */
	{ 0x1E, KEY_CAMERA },
	{ 0x1F, KEY_F18 },		/* information */
	{ 0x20, KEY_SWITCHVIDEOMODE },	/* PIP */
	{ 0x23, KEY_MINUS },		/* plus 100 */
	{ 0x25, KEY_SOUND },		/* caption */
	{ 0x27, KEY_ALTERASE },		/* AD */
	{ 0x28, KEY_COPY },		/* picture mode */
	{ 0x2B, KEY_F19 },		/* S mode */
	{ 0x2C, KEY_F22 },		/* TTX MIX */
	{ 0x2D, KEY_EXIT },
	{ 0x32, KEY_FORWARD },		/* PIP channel up */
	{ 0x33, KEY_CLOSECD },		/* PIP channel down */
	{ 0x36, KEY_EJECTCD },		/* antena */
	{ 0x37, KEY_WWW },		/* WebBrowser */
	{ 0x3A, KEY_EJECTCLOSECD },	/* auto program */
	{ 0x3B, KEY_SETUP },		/* factory */
	{ 0x3C, KEY_F20 },		/* 3 speed */
	{ 0x3E, KEY_FRONT },		/* picture size */
	{ 0x3F, KEY_HELP },		/* e-manual */
	{ 0x40, KEY_COMPUTER },		/* game mode */
	{ 0x42, KEY_SCREENLOCK },	/* capture */
	{ 0x43, KEY_DELETEFILE },	/* DTV key */
	{ 0x44, KEY_F13 },		/* favorite channel */
	{ 0x45, KEY_REWIND },		/* rewind */
	{ 0x46, KEY_STOPCD },
	{ 0x47, KEY_PLAYCD },
	{ 0x48, KEY_FASTFORWARD },	/* fast forward */
	{ 0x49, KEY_RECORD },
	{ 0x4A, KEY_PAUSECD },		/* pause */
	{ 0x4B, KEY_COMPOSE },		/* simple menu */
	{ 0x4C, KEY_PLAYPAUSE },		/* MBR setup */
	{ 0x4E, KEY_BRIGHTNESSUP },	/* fast forward */
	{ 0x4F, KEY_PROPS },		/* program information */
	{ 0x50, KEY_BRIGHTNESSDOWN },	/* rewind */
	{ 0x51, KEY_PHONE },		/* game mode 3D */
	{ 0x53, KEY_QUESTION },		/* zoom 1 */
	{ 0x58, KEY_ESC },		/* return(back) */
	{ 0x59, KEY_PLAY },		/* subtitle */
	{ 0x5A, KEY_CLOSE },		/* MBR clear */
	{ 0x5C, KEY_AGAIN },		/* MBR repeat */
	{ 0x60, KEY_UP },
	{ 0x61, KEY_DOWN },
	{ 0x62, KEY_RIGHT },
	{ 0x64, KEY_BASSBOOST },		/* SNS */
	{ 0x65, KEY_LEFT },
	{ 0x68, KEY_ENTER },
	{ 0x6B, KEY_F7 },		/* channel list */
	{ 0x6C, KEY_F1 },		/* A (red) */
	{ 0x6E, KEY_F23 },		/* SRS */
	{ 0x72, KEY_PRINT },		/* DVR */
	{ 0x73, KEY_SEARCH },		/* smart search */
	{ 0x76, KEY_F5 },		/* smart home */
	{ 0x77, KEY_SUSPEND },		/* energy saving */
	{ 0x79, KEY_F5 },		/* smart home */
	{ 0x8B, KEY_PASTE },		/* HDMI */
	{ 0x8C, KEY_F5 },		/* home panel */
	{ 0x8F, KEY_HP },		/* app list */
	{ 0x9A, KEY_REPLY },		/* HDMI CEC */
	{ 0x9C, KEY_CALC },		/* keypad (more) */
	{ 0x9F, KEY_F21 },		/* 3D */
	{ 0xA0, KEY_CHAT },		/* BT voice */
	{ 0xA4, KEY_FIND },		/* USB hub */
	{ 0xA6, KEY_PROG4 },		/* home panel */
	{ 0xA8, KEY_VIDEO_NEXT },	/* page left */
	{ 0xA9, KEY_VIDEO_PREV },	/* page right */
	{ 0xB3, KEY_PROG3 },		/* dual view */
	{ 0xB5, KEY_F14 },		/* recommend & search */
	{ 0xB6, KEY_F24 },		/* WIFI pairing */
	{ 0xB7, KEY_EDIT },		/* MBR guide */
	{ 0xB8, KEY_SPORT },		/* soccer mode */
	{ 0xC0, KEY_KPLEFTPAREN },	/* MBR setup failure */
	{ 0xC6, KEY_SEND },		/* family story */
	{ 0xC8, KEY_KPRIGHTPAREN },	/* MBR setup */
	{ 0xC9, KEY_NEW },		/* MBR watch TV */
	{ 0xCA, KEY_KBDILLUMTOGGLE },	/* MBR watch movie */
	{ 0xCC, KEY_KBDILLUMUP },	/* MBR setup confirm */
	{ 0xD0, KEY_BOOKMARKS },		/* BT contents bar */
	{ 0xD1, KEY_DIRECTION },		/* BT trigger */
	{ 0xD2, KEY_F15 },		/* number */
	{ 0xD3, KEY_CYCLEWINDOWS },	/* BT hotkey */
	{ 0xD4, KEY_MAIL },		/* BT device */
	{ 0xD5, KEY_MSDOS },		/* BT color mecha */
	{ 0xD9, KEY_SCROLLDOWN },	/* MBR BD/DVD power */
	{ 0xE0, KEY_XFER },		/* STB power */
	{ 0xF0, KEY_KBDILLUMDOWN },	/* MBR STB/BD menu */
	{ 0xF1, KEY_SCROLLUP },		/* MBR BD popup */
	{ 0xF2, KEY_MOVE },		/* MBR TV */
	{ 0xF3, KEY_MEDIA },		/* Netflix */
	{ 0xF4, KEY_SHOP },		/* Amazon */
	{ 0xAF, KEY_DISPLAY_OFF },		/* Extra Key */
	{ 0xAA, KEY_DASHBOARD },		/* KEY_FAMILY_MODE */
	{ 0xBA, KEY_MAIL },		/* KEY_CINEMA_MODE */
	{ 0xB2, KEY_PROG3 },	/* dual view */
};

static struct rc_map_list wt61p807_map = {
	.map = {
		.scan    = wt61p807,
		.size    = ARRAY_SIZE(wt61p807),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_WT61P807,
	}
};

static int __init init_rc_map_wt61p807(void)
{
	return rc_map_register(&wt61p807_map);
}

static void __exit exit_rc_map_wt61p807(void)
{
	rc_map_unregister(&wt61p807_map);
}

module_init(init_rc_map_wt61p807)
module_exit(exit_rc_map_wt61p807)

MODULE_LICENSE("GPL");
