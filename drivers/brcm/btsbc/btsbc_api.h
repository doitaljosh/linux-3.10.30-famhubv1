/*
 *
 * btsbc_api.h
 *
 *
 *
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation (the "GPL"), and may
 * be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL for more details.
 *
 *
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php
 * or by writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
 *
 *
 */

#ifndef BTSBC_H
#define BTSBC_H

/*******************************************************************************
 **
 ** Function         btsbc_alloc
 **
 ** Description      BTSBC Module Allocation function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
extern int btsbc_alloc(void);

/*******************************************************************************
 **
 ** Function         btsbc_free
 **
 ** Description      BTSBC Module Free function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
extern int btsbc_free(int sbc_channel);

/*******************************************************************************
 **
 ** Function         btsbc_config
 **
 ** Description      BTSBC Module Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_config(int sbc_channel, int frequency, unsigned char blocks,
        unsigned char subbands, unsigned char mode, unsigned char allocation, unsigned char bitpool);

/*******************************************************************************
 **
 ** Function         btsbc_module_init
 **
 ** Description      BTSBC Module Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btsbc_encode(int sbc_channel, const void *input, unsigned int input_len,
            void *output, unsigned int output_len, int *p_written);

#endif
