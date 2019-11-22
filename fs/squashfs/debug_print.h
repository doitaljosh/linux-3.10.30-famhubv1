/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * debug_print.h
 */

typedef struct buffer_head * (*get_block_length_t)(
	struct super_block *sb,
	u64 *cur_index, int *offset,
	int *length);

struct debug_print_state {
	struct super_block *sb;
	struct buffer_head **bh;
	unsigned int b;
	int block_type;
	u64 index;
	int compressed;
	int srclength;
	u64 *next_index;
	u64 __cur_index;
	int __offset;
	int __length;
	get_block_length_t get_block_length;
};
