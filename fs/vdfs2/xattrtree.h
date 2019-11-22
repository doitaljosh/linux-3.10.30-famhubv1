/**
 * @file	fs/emmcfs/xattrtree.h
 * @brief	Basic B-tree operations - interfaces and prototypes.
 * @date	31/06/2013
 *
 * eMMCFS -- Samsung eMMC chip oriented File System, Version 1.
 *
 * This file contains prototypes, constants and enumerations extended attributes
 * tree
 *
 * Copyright 2011 by Samsung Electronics, Inc.,
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics, Inc. ("Confidential Information").  You
 * shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung.
 */

#ifndef XATRTREE_H_
#define XATRTREE_H_

#ifdef USER_SPACE
#include "vdfs_tools.h"
#endif

#include "vdfs_layout.h"

#define XATTRTREE_LEAF "XAle"
#define VDFS_XATTRTREE_ROOT_REC_NAME "xattr_root"


struct vdfs_xattrtree_record {
	struct vdfs_xattrtree_key *key;
	void *val;
};


#endif

