/*
 * Copyright (C) 2008-2009 Palm, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/version.h>

#define MEMNOTIFY_DEVICE   "memnotify"

#define MEMNOTIFY_NORMAL   0x0000
#define MEMNOTIFY_LOW	   0xfaac
#define MEMNOTIFY_CRITICAL 0xdead
#define MEMNOTIFY_DEADLY   0xb00f

#ifdef CONFIG_SLP_LOWMEM_NOTIFY
extern void memnotify_threshold(gfp_t gfp_mask, int nr_pages);
#else
static inline void memnotify_threshold(gfp_t gfp_mask) { }
#endif


#define LOG_LV_FORCE        -1
#define LOG_LV_OFF          0
#define LOG_LV_INFO         1
#define LOG_LV_VERBOSE      2
#define LOG_LV_TRACE        3



#define LOG_TAG             "[LMN]"

#define __LOG_PRINT(lv, args...) { \
    do { \
        if( lv<=LOG_LV_INFO ) pr_err(LOG_TAG args); \
        else if( lv<=LOG_LV_INFO ) pr_warning(LOG_TAG args); \
        else if( lv<=LOG_LV_VERBOSE ) pr_notice(LOG_TAG args); \
        else pr_info(LOG_TAG args); \
    } while(0); \
}


#define ERR_PRINT(args...) { \
    do { \
        pr_crit(LOG_TAG "[ERR]" args); \
    } while(0); \
}

