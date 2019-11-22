/*
 * kdml_rb_tree.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#ifndef __KDML_RB_TREE_H__
#define __KDML_RB_TREE_H__

#define KEY	value.ptr
typedef uint32_t key_type; /* for keeping any type of data in rbtree */
typedef struct kdml_alloc_info value_type; /* for keeping any type of data in rbtree */

struct kdml_rb_type {
	struct rb_node rb_node;
	unsigned long reserved; /* needed for alignment */
	value_type value;
};

extern int rbtree_insert(struct rb_root *root, struct kdml_rb_type *node);
extern struct kdml_rb_type *rbtree_search(struct rb_root *root, key_type key);
extern void rbtree_delete(struct rb_root *root, struct kdml_rb_type *node);

/* summary_mode rb tree */
extern int summary_rbtree_insert(struct rb_root *root,
		struct kdml_summary_type *node);
extern struct kdml_summary_type *summary_rbtree_search(struct rb_root *root,
		unsigned long call_site);
extern void summary_rbtree_delete(struct rb_root *root,
		struct kdml_summary_type *node);

#endif /* __KDML_RB_TREE_H__ */

