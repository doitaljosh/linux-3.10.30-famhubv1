/**
 * VDFS -- Vertically Deliberate improved performance File System
 *
 * Copyright 2012 by Samsung Electronics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include "emmcfs.h"
#include "debug.h"

/**
 * The VDFS mount options.
 */
enum {
	option_readonly,
	option_count,
	option_fmask,
	option_dmask,
	option_do_not_check_sign,
	option_error


};

/**
 * The VDFS mount options match tokens.
 */
static const match_table_t tokens = {
	{option_readonly, "ro"},
	{option_count, "count=%u"},
	{option_fmask, "fmask=%o"},
	{option_dmask, "dmask=%o"},
	{option_do_not_check_sign, "dncs"},
	{option_error, NULL}
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

	if (!input)
		return 0;

	while ((p = strsep(&input, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {

		case option_count:
			if (match_int(&args[0], &option))
				BUG();

			EMMCFS_DEBUG_TMP("counter %d", option);
			VDFS_SB(sb)->bugon_count = (int)option;
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
			VDFS_SB(sb)->fmask = (umode_t)option;
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
			VDFS_SB(sb)->dmask = (umode_t)option;
			break;
		case option_do_not_check_sign:
			set_option(VDFS_SB(sb), DO_NOT_CHECK_SIGN);
			set_sbi_flag(VDFS_SB(sb), DO_NOT_CHECK_SIGN);
			break;
		default:
			return -EINVAL;
		}
	}
	EMMCFS_DEBUG_SB("finished (ret = %d)", ret);
	return ret;
}

static const char *token2str(int token)
{
	const struct match_token *t;

	for (t = tokens; t->token != option_error; t++)
		if (t->token == token)
			break;
	return t->pattern;
}


int vdfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct vdfs_sb_info *sbi = VDFS_SB(root->d_sb);

#define SEQ_PUT_OPT(token) do {\
	seq_putc(seq, ','); \
	seq_puts(seq, token2str(token)); \
} while (0)

#define SEQ_PUT_ARG(fmt, ...) seq_printf(seq, "," fmt, ##__VA_ARGS__)

	if (is_sbi_flag_set(sbi, DO_NOT_CHECK_SIGN))
		SEQ_PUT_OPT(option_do_not_check_sign);

	if (test_option(sbi, FMASK))
		SEQ_PUT_ARG("fmask=%o", sbi->fmask);

	if (test_option(sbi, DMASK))
		SEQ_PUT_ARG("dmask=%o", sbi->dmask);

	return 0;
}

