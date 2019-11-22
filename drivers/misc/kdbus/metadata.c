/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/cgroup.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/path.h>
#include <linux/pid_namespace.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "connection.h"
#include "message.h"
#include "metadata.h"
#include "names.h"
#include "compatibility/compatibility.h"

/**
 * kdbus_meta_new() - create new metadata object
 * @meta:		New metadata object
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_meta_new(struct kdbus_meta **meta)
{
	struct kdbus_meta *m;

	BUG_ON(*meta);

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	/*
	 * Remember the PID domain our credentials belong to; we
	 * need to prevent leaking authorization and security-relevant
	 * data across different domains.
	 */
	m->domain = task_active_pid_ns(current);

	*meta = m;
	return 0;
}

/**
 * kdbus_meta_free() - release metadata
 * @meta:		Metadata object
 */
void kdbus_meta_free(struct kdbus_meta *meta)
{
	if (!meta)
		return;

	kfree(meta->data);
	kfree(meta);
}

static struct kdbus_item *
kdbus_meta_append_item(struct kdbus_meta *meta, size_t extra_size)
{
	struct kdbus_item *item;
	size_t size;

	/* get new metadata buffer, pre-allocate at least 512 bytes */
	if (!meta->data) {
		size = roundup_pow_of_two(256 + KDBUS_ALIGN8(extra_size));
		meta->data = kzalloc(size, GFP_KERNEL);
		if (!meta->data)
			return ERR_PTR(-ENOMEM);

		meta->allocated_size = size;
	}

	/* double the pre-allocated buffer size if needed */
	size = meta->size + KDBUS_ALIGN8(extra_size);
	if (size > meta->allocated_size) {
		size_t size_diff;
		struct kdbus_item *data;

		size = roundup_pow_of_two(size);
		size_diff = size - meta->allocated_size;
		data = kmalloc(size, GFP_KERNEL);
		if (!data)
			return ERR_PTR(-ENOMEM);

		memcpy(data, meta->data, meta->size);
		memset((u8 *)data + meta->allocated_size, 0, size_diff);

		kfree(meta->data);
		meta->data = data;
		meta->allocated_size = size;
	}

	/* insert new record */
	item = (struct kdbus_item *)((void*)((u8 *)meta->data + meta->size));
	meta->size += KDBUS_ALIGN8(extra_size);

	return item;
}

/**
 * kdbus_meta_append_data() - append given raw data to metadata object
 * @meta:		Metadata object
 * @type:		KDBUS_ITEM_* type
 * @data:		pointer to data to copy from
 * @len:		number of bytes to copy
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_meta_append_data(struct kdbus_meta *meta, u64 type,
			   const void *data, size_t len)
{
	struct kdbus_item *item;
	u64 size;

	if (len == 0)
		return 0;

	size = KDBUS_ITEM_SIZE(len);
	item = kdbus_meta_append_item(meta, (size_t)size);
	if (IS_ERR(item))
		return PTR_ERR(item);

	item->type = type;
	item->size = KDBUS_ITEM_HEADER_SIZE + len;
	memcpy(item->data, data, len);

	return 0;
}

static int kdbus_meta_append_str(struct kdbus_meta *meta, u64 type,
				 const char *str)
{
	return kdbus_meta_append_data(meta, type, str, strlen(str) + 1);
}

static int kdbus_meta_append_timestamp(struct kdbus_meta *meta,
				       u64 seq)
{
	struct kdbus_item *item;
	u64 size = KDBUS_ITEM_SIZE(sizeof(struct kdbus_timestamp));
	struct timespec ts;

	item = kdbus_meta_append_item(meta, (size_t)size);
	if (IS_ERR(item))
		return PTR_ERR(item);

	item->type = KDBUS_ITEM_TIMESTAMP;
	item->size = size;

	if (seq > 0)
		item->timestamp.seqnum = seq;

	ktime_get_ts(&ts);
	item->timestamp.monotonic_ns = (__u64)timespec_to_ns(&ts);

	ktime_get_real_ts(&ts);
	item->timestamp.realtime_ns = (__u64)timespec_to_ns(&ts);

	return 0;
}

static int kdbus_meta_append_cred(struct kdbus_meta *meta)
{
	struct kdbus_creds creds;

	creds.uid = from_kuid(current_user_ns(), current_uid());
	creds.gid = from_kgid(current_user_ns(), current_gid());
	creds.pid = (__u64)task_pid_vnr(current);
	creds.tid = (__u64)task_tgid_vnr(current);
	creds.starttime = (__u64)timespec_to_ns(&current->start_time);

	return kdbus_meta_append_data(meta, KDBUS_ITEM_CREDS,
				      &creds, sizeof(creds));
}

static int kdbus_meta_append_src_names(struct kdbus_meta *meta,
				       struct kdbus_conn *conn)
{
	struct kdbus_name_entry *e;
	int ret = 0;

	if (!conn)
		return 0;

	mutex_lock(&conn->lock);
	list_for_each_entry(e, &conn->names_list, conn_entry) {
		struct kdbus_item *item;
		size_t len;
		size_t size;

		len = strlen(e->name) + 1;
		size = KDBUS_ITEM_SIZE(sizeof(struct kdbus_name) + len);

		item = kdbus_meta_append_item(meta, size);
		if (IS_ERR(item)) {
			ret = PTR_ERR(item);
			break;
		}

		item->type = KDBUS_ITEM_NAME;
		item->size = KDBUS_ITEM_HEADER_SIZE +
			     sizeof(struct kdbus_name) + len;
		item->name.flags = e->flags;
		memcpy(item->name.name, e->name, len);
	}
	mutex_unlock(&conn->lock);

	return ret;
}

static int kdbus_meta_append_exe(struct kdbus_meta *meta)
{
	struct mm_struct *mm = get_task_mm(current);
	struct path *exe_path = NULL;
	char *pathname;
	int ret = 0;
	size_t len;
	char *tmp;
	unsigned long int addr;

	if (mm) {
		down_read(&mm->mmap_sem);
		if (mm->exe_file) {
			path_get(&mm->exe_file->f_path);
			exe_path = &mm->exe_file->f_path;
		}
		up_read(&mm->mmap_sem);
	}

	if (!exe_path)
		goto exit_mmput;

	addr = __get_free_page(GFP_TEMPORARY | __GFP_ZERO);
	tmp = (char *)addr;
	if (!tmp) {
		ret = -ENOMEM;
		goto exit_path_put;
	}

	pathname = d_path(exe_path, tmp, PAGE_SIZE);
	if (IS_ERR(pathname)) {
		ret = PTR_ERR(pathname);
		goto exit_free_page;
	}

	len = (size_t)(tmp + PAGE_SIZE - pathname);
	ret = kdbus_meta_append_data(meta, KDBUS_ITEM_EXE, pathname, len);

exit_free_page:
	free_page((unsigned long) tmp);

exit_path_put:
	path_put(exe_path);

exit_mmput:
	if (mm) {
		mmput(mm);
	}
	return ret;
}

static int kdbus_meta_append_cmdline(struct kdbus_meta *meta)
{
	struct mm_struct *mm;
	int ret = 0;
	size_t len;
	char *tmp;
	unsigned long int addr;

	addr = __get_free_page(GFP_TEMPORARY | __GFP_ZERO);
	tmp = (char *)addr;
	if (!tmp)
		return -ENOMEM;

	mm = get_task_mm(current);
	if (!mm)
		goto exit_free_page;

	if (!mm->arg_end)
		goto exit_mmput;

	len = mm->arg_end - mm->arg_start;
	if (len > PAGE_SIZE)
		len = PAGE_SIZE;

	ret = copy_from_user(tmp, (const char __user *)mm->arg_start, len);
	if (ret < 0)
		goto exit_mmput;

	ret = kdbus_meta_append_data(meta, KDBUS_ITEM_CMDLINE, tmp, len);

exit_mmput:
	mmput(mm);

exit_free_page:
	free_page((unsigned long) tmp);

	return ret;
}

static int kdbus_meta_append_caps(struct kdbus_meta *meta)
{
	struct caps {
		u32 cap[_KERNEL_CAPABILITY_U32S];
	} cap[4];
	unsigned int i;
	const struct cred *cred = current_cred();

	for (i = 0; i < _KERNEL_CAPABILITY_U32S; i++) {
		cap[0].cap[i] = cred->cap_inheritable.cap[i];
		cap[1].cap[i] = cred->cap_permitted.cap[i];
		cap[2].cap[i] = cred->cap_effective.cap[i];
		cap[3].cap[i] = cred->cap_bset.cap[i];
	}

	/* clear unused bits */
	for (i = 0; i < 4; i++)
		cap[i].cap[CAP_TO_INDEX(CAP_LAST_CAP)] &=
			CAP_TO_MASK(CAP_LAST_CAP + 1) - 1;

	return kdbus_meta_append_data(meta, KDBUS_ITEM_CAPS,
				      cap, sizeof(cap));
}

#ifdef CONFIG_CGROUPS
static int kdbus_meta_append_cgroup(struct kdbus_meta *meta)
{
	char *buf, *path;
	int ret;
	unsigned long int addr;

	addr = __get_free_page(GFP_TEMPORARY | __GFP_ZERO);
	buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	path = task_cgroup_path(current, buf, PAGE_SIZE);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
        ret = task_cgroup_path_from_hierarchy(current, 0, buf, PAGE_SIZE);
#else
	ret = task_cgroup_path(current, buf, PAGE_SIZE);
#endif
	if (ret < 0)
		path = NULL;
	else
		path = buf;
#endif

	if (path)
		ret = kdbus_meta_append_str(meta, KDBUS_ITEM_CGROUP, path);
	else
		ret = -ENAMETOOLONG;

	free_page((unsigned long) buf);

	return ret;
}
#endif

#ifdef CONFIG_AUDITSYSCALL
static int kdbus_meta_append_audit(struct kdbus_meta *meta)
{
	struct kdbus_audit audit;

	audit.loginuid = from_kuid(current_user_ns(),
				   audit_get_loginuid(current));
	audit.sessionid = audit_get_sessionid(current);

	return kdbus_meta_append_data(meta, KDBUS_ITEM_AUDIT,
				      &audit, sizeof(audit));
}
#endif

#ifdef CONFIG_SECURITY
static int kdbus_meta_append_seclabel(struct kdbus_meta *meta)
{
	u32 len, sid;
	char *label;
	int ret;

	security_task_getsecid(current, &sid);
	ret = security_secid_to_secctx(sid, &label, &len);
	if (ret == -EOPNOTSUPP)
		return 0;
	if (ret < 0)
		return ret;

	if (label && len > 0)
		ret = kdbus_meta_append_data(meta, KDBUS_ITEM_SECLABEL,
					     label, len);
	security_release_secctx(label, len);

	return ret;
}
#endif

/**
 * kdbus_meta_append() - collect metadata from current process
 * @meta:		Metadata object
 * @conn:		Current connection to read names from
 * @seq:		Message sequence number
 * @which:		KDBUS_ATTACH_* flags which typ of data to attach
 *
 * Collect the data specified in flags and allocate or extend
 * the buffer in the metadata object.
 *
 * Return: 0 on success, negative errno on failure.
 */
int kdbus_meta_append(struct kdbus_meta *meta,
		      struct kdbus_conn *conn,
		      u64 seq, u64 which)
{
	int ret;
	u64 mask;

	/* which metadata is wanted but not yet attached? */
	mask = which & ~meta->attached;
	if (mask == 0)
		return 0;

	if (mask & KDBUS_ATTACH_TIMESTAMP) {
		ret = kdbus_meta_append_timestamp(meta, seq);
		if (ret < 0)
			return ret;
	}

	if (mask & KDBUS_ATTACH_CREDS) {
		ret = kdbus_meta_append_cred(meta);
		if (ret < 0)
			return ret;
	}

	if (mask & KDBUS_ATTACH_NAMES && conn) {
		ret = kdbus_meta_append_src_names(meta, conn);
		if (ret < 0)
			return ret;
	}

	if (mask & KDBUS_ATTACH_COMM) {
		char comm[TASK_COMM_LEN];

		get_task_comm(comm, current->group_leader);
		ret = kdbus_meta_append_str(meta, KDBUS_ITEM_TID_COMM, comm);
		if (ret < 0)
			return ret;

		get_task_comm(comm, current);
		ret = kdbus_meta_append_str(meta, KDBUS_ITEM_PID_COMM, comm);
		if (ret < 0)
			return ret;
	}

	if (mask & KDBUS_ATTACH_EXE) {
		ret = kdbus_meta_append_exe(meta);
		if (ret < 0)
			return ret;
	}

	if (mask & KDBUS_ATTACH_CMDLINE) {
		ret = kdbus_meta_append_cmdline(meta);
		if (ret < 0)
			return ret;
	}

	/* we always return a 4 elements, the element size is 1/4  */
	if (mask & KDBUS_ATTACH_CAPS) {
		ret = kdbus_meta_append_caps(meta);
		if (ret < 0)
			return ret;
	}

#ifdef CONFIG_CGROUPS
	/* attach the path of the one group hierarchy specified for the bus */
	if (mask & KDBUS_ATTACH_CGROUP) {
		ret = kdbus_meta_append_cgroup(meta);
		if (ret < 0)
			return ret;
	}
#endif

#ifdef CONFIG_AUDITSYSCALL
	if (mask & KDBUS_ATTACH_AUDIT) {
		ret = kdbus_meta_append_audit(meta);
		if (ret < 0)
			return ret;
	}
#endif

#ifdef CONFIG_SECURITY
	if (mask & KDBUS_ATTACH_SECLABEL) {
		ret = kdbus_meta_append_seclabel(meta);
		if (ret < 0)
			return ret;
	}
#endif

	if ((mask & KDBUS_ATTACH_CONN_NAME) && conn && conn->name) {
		ret = kdbus_meta_append_str(meta, KDBUS_ITEM_CONN_NAME,
					    conn->name);
		if (ret < 0)
			return ret;
	}

	/*
	 * We tried to add everything we got asked for; do not get
	 * here again for the same question.
	 */
	meta->attached |= mask;

	return 0;
}

#ifdef SBB_SUPPORT
int kdbus_kmsg_append_sbb_dest_name(struct kdbus_meta *meta, const char* dst_name)
{
	return kdbus_meta_append_str(meta, KDBUS_ITEM_SBB_DST_NAME, dst_name);
}

int kdbus_kmsg_append_sbb_bloom_filter(struct kdbus_meta *meta, const struct kdbus_bloom_filter *bloom_filter, u64 bloom_size)
{
	if (meta->attached_for_sbb & KDBUS_ATTACH_SBB_BLOOM_FILTER)
		return 0;

	meta->attached_for_sbb |= KDBUS_ATTACH_SBB_BLOOM_FILTER;
	return kdbus_meta_append_data(meta, KDBUS_ITEM_SBB_BLOOM_FILTER, bloom_filter, bloom_size);
}
#endif
