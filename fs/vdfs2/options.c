/**
 * @file	fs/emmcfs/options.c
 * @brief	The eMMCFS mount options parsing routines.
 * @author	Dmitry Voytik, d.voytik@samsung.com
 * @date	01/19/2012
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file implements eMMCFS parsing options
 *
 * @see		TODO: documents
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#include <linux/string.h>
#include <linux/parser.h>
#include "emmcfs.h"
#include "debug.h"

/**
 * The VDFS mount options.
 */
enum {
	option_novercheck,
	option_btreecheck,
	option_readonly,
	option_debugcheck,
	option_count,
	option_tinysmall, /* disable tiny and small files */
	option_tiny, /* disable only small files */
	option_stripped,
	option_fmask,
	option_dmask,
	option_error
};

/**
 * The VDFS mount options match tokens.
 */
static const match_table_t tokens = {
	{option_novercheck, "novercheck"},
	{option_btreecheck, "btreecheck"},
	{option_readonly, "ro"},
	{option_debugcheck, "debugcheck"},
	{option_count, "count=%u"},
	{option_tinysmall, "tinysmall"},
	{option_tiny, "tiny"},
	{option_stripped, "stripped"},
	{option_fmask, "fmask=%o"},
	{option_dmask, "dmask=%o"},
	{option_error, NULL},
};

#define VDFS_MASK_LEN 3

/**
 * @brief		Parse eMMCFS options.
 * @param [in]	sb	VFS super block
 * @param [in]	input	Options string for parsing
 * @return		Returns 0 on success, errno on failure
 */
int emmcfs_parse_options(struct super_block *sb, char *input)
{
	int ret = 0;
	int token;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	unsigned int option;

	set_option(VDFS_SB(sb), VERCHECK);

	if (!input)
		return 0;

	while ((p = strsep(&input, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {

		case option_novercheck:
			clear_option(VDFS_SB(sb), VERCHECK);
			printk(KERN_WARNING "[VDFS-warning] Checking versions"
					" of driver and mkfs is disabled\n");

			break;

		case option_btreecheck:
			set_option(VDFS_SB(sb), BTREE_CHECK);
			break;

		case option_debugcheck:
			set_option(VDFS_SB(sb), DEBUG_AREA_CHECK);
			break;
		case option_tiny:
			set_option(VDFS_SB(sb), TINY);
			break;
		case option_tinysmall:
			set_option(VDFS_SB(sb), TINYSMALL);
			break;
		case option_count:
			if (match_int(&args[0], &option))
				BUG();

			EMMCFS_DEBUG_TMP("counter %d", option);
			VDFS_SB(sb)->bugon_count = option;
			break;
		case option_stripped:
			VDFS_SET_READONLY(sb);
			set_option(VDFS_SB(sb), STRIPPED);
			break;
		case option_fmask:
			ret = match_octal(&args[0], &option);
			if (ret) {
				printk(KERN_ERR "[VDFS-ERROR] fmask must be "
						"octal-base value");
				return ret;
			}
			if (option & ~((unsigned int) S_IRWXUGO)) {
				printk(KERN_ERR "[VDFS-ERROR] fmask  "
					" is wrong");
				return -EINVAL;
			}
			set_option(VDFS_SB(sb), FMASK);
			VDFS_SB(sb)->fmask = option;
			break;
		case option_dmask:
			ret = match_octal(&args[0], &option);
			if (ret) {
				printk(KERN_ERR "[VDFS-ERROR] dmask must be "
						"octal-base value");
				return ret;
			}
			if (option & ~((unsigned int) S_IRWXUGO)) {
				printk(KERN_ERR "[VDFS-ERROR] dmask  "
					" is wrong");
				return -EINVAL;
			}

			set_option(VDFS_SB(sb), DMASK);
			VDFS_SB(sb)->dmask = option;
			break;
		default:
			return -EINVAL;
		}
	}
	EMMCFS_DEBUG_SB("finished (ret = %d)", ret);
	return ret;
}
