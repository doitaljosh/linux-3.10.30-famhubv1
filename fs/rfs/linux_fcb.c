/*
 * RFS 3.0 Developed by Flash Software Group.
 *
 * Copyright 2006-2009 by Memory Division, Samsung Electronics, Inc.,
 * San #16, Banwol-Dong, Hwasung-City, Gyeonggi-Do, Korea
 *
 * All rights reserved.
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

/**
 * @file        linux_fcb.c
 * @brief       This file includes APIs to NativeFS for accessing file structure.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_fcb.h"

#include <linux/module.h>

EXPORT_SYMBOL(FcbGetVnode);
EXPORT_SYMBOL(FcbGetOffset);
EXPORT_SYMBOL(FcbIsSyncMode);
EXPORT_SYMBOL(FcbIsModtimeUpdate);
EXPORT_SYMBOL(FcbIsAcstimeUpdate);

// end of file
