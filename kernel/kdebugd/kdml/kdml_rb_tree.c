/*
 * kdml_rb_tree.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 * reference: Documentation/rbtree.txt
 */

#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include "kdml/kdml_packet.h"
#include "kdml/kdml_rb_tree.h"

/*
 * Detailed Level RB-tree:
 * We need 2 sets of functions as their data structures are different
 */
int rbtree_insert(struct rb_root *root, struct kdml_rb_type *data)
{
	struct rb_node **new, *parent = NULL;

	BUG_ON(!root);
	BUG_ON(!data);

	new = &root->rb_node;

	while (*new) {
		struct kdml_rb_type *this = container_of(*new, struct kdml_rb_type, rb_node);
		parent = *new;
		if (this->KEY == data->KEY)
			return -1;

		if (this->KEY > data->KEY)
			new = &(*new)->rb_left;
		else
			new = &(*new)->rb_right;
	}
	rb_link_node(&data->rb_node, parent, new);
	rb_insert_color(&data->rb_node, root);

	return 0;
}

struct kdml_rb_type *rbtree_search(struct rb_root *root, key_type key)
{
	struct rb_node *node;

	BUG_ON(!root);

	node = root->rb_node;

	while (node) {
		struct kdml_rb_type *data = container_of(node, struct kdml_rb_type, rb_node);
		if (data->KEY > key)
			node = node->rb_left;
		else if (data->KEY < key)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

void rbtree_delete(struct rb_root *root, struct kdml_rb_type *data)
{
	BUG_ON(!root);
	BUG_ON(!data);
	rb_erase(&data->rb_node, root);
}

/*
 * DB 2: RB-tree of unique callers (it is summarized database)
 *  Supported functions:
 *      summary_rbtree_insert
 *      summary_rbtree_search
 *      summary_rbtree_delete
 */
int summary_rbtree_insert(struct rb_root *root, struct kdml_summary_type *data)
{
	struct rb_node **new, *parent = NULL;

	BUG_ON(!root);
	BUG_ON(!data);

	new = &root->rb_node;

	while (*new) {
		struct kdml_summary_type *this = container_of(*new, struct kdml_summary_type, node);
		parent = *new;
		/* caller should search first and then insert */
		if (this->summary.call_site == data->summary.call_site) {
			BUG();
			return -1;
		}
		if (this->summary.call_site > data->summary.call_site)
			new = &(*new)->rb_left;
		else
			new = &(*new)->rb_right;
	}
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return 0;
}

struct kdml_summary_type *summary_rbtree_search(struct rb_root *root, unsigned long call_site)
{
	struct rb_node *node;

	BUG_ON(!root);

	node = root->rb_node;

	while (node) {
		struct kdml_summary_type *data;
		data = container_of(node, struct kdml_summary_type, node);
		if (data->summary.call_site > call_site)
			node = node->rb_left;
		else if (data->summary.call_site < call_site)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

void summary_rbtree_delete(struct rb_root *root, struct kdml_summary_type *data)
{
	BUG_ON(!root);
	BUG_ON(!data);

	rb_erase(&data->node, root);
}

