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
 * @file        linux_xattr.c
 * @brief       This file includes extended attribute operations.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */


#include "linux_xattr.h"
#include "linux_vnode.h"

#include <linux/xattr.h>
#include <linux/security.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_XATTR)

/******************************************************************************/
/* Internal Functions                                                         */
/******************************************************************************/
/**
 * @brief	get the value of a specific extended attribute, or compute the buffer size required
 *
 * @param[in]	pVnode	Vnode to get extended attributes
 * @param[in]	dwID	id of namespace 
 * @param[in]	psName	name of extended attribute
 * @param[out]	pValue	buffer to return the value of extended attribute
 * @param[in]	szValueSize the size of buffer (if zero, it returns the current size of attr)
 * @returns		the number of bytes used/required on success, or a negative error number on falure
 *
 * If pValue is Null, return the number of bytes required for buffer.
 */
int 
XattrCommGetValue(
	PVNODE			pVnode,
	XATTR_NAMESPACE_ID	dwID,
	const char*		psName,
	void*			pValue,
	LINUX_SIZE_T 	szValueSize)
{
	FERROR			nErr = FERROR_NO_ERROR;
	unsigned int	dwSizeRead = 0;

	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* check operation */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnGetXAttribute))
	{
		LNX_CMZ(("No Native interface for getting xattr value"));
		return -ENOSYS;
	}

	/* get NativeFS's xattr value */
	nErr = pVnode->pVnodeOps->pfnGetXAttribute(pVnode,
												psName,
												pValue,
												szValueSize,
												dwID,
												&dwSizeRead);
	if (nErr != FERROR_NO_ERROR)
	{
		/* if xttr doesn't exist, return -ENODATA to VFS */
		LNX_EMZ(("NativeFS GetXattr(%d:%s) fails(nErr: %08x), vnodeID(%016llx)",
					dwID, psName, -nErr, pVnode->llVnodeID));
		return RtlLinuxError(nErr);
	}

	return dwSizeRead;

}

/**
 * @brief	set the value of a specific extended attribute
 *
 * @param[in]	pVnode	Vnode to set extended attributes
 * @param[in]	dwID	id of namespace 
 * @param[in]	psName	name of extended attribute
 * @param[in]	pValue	buffer having the value of extended attribute
 * @param[in]	szValueSize	the size of buffer
 * @param[in]	dwFlags flag for creation or replacement
 * @returns		0 on success, or a negative error number on failure
 */
int
XattrCommSetValue(
	PVNODE				pVnode,
	XATTR_NAMESPACE_ID	dwID,
	const char*			psName,
	const void*			pValue,
	LINUX_SIZE_T		szValueSize,
	int					dwFlags)
{
	FERROR			nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* check operation */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnSetXAttribute))
	{
		LNX_CMZ(("No Native interface for setting xattr value"));
		return -ENOSYS;
	}

	/*
	 * when dwFlag set XATTR_CREATE, fail if attr already exists
	 * when dwFlag set XATTR_REPLACE, fail if attr does not exist
	 */
	nErr = pVnode->pVnodeOps->pfnSetXAttribute(pVnode,
												psName,
												pValue,
												szValueSize,
												dwID,
												dwFlags);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS SetXattr(%d:%s) fails(nErr: %08x): "
					"pValue:%p, szValueSize:%d, dwFlags:%u",
					dwID, psName, -nErr,
					pValue, szValueSize, dwFlags));

		return RtlLinuxError(nErr);
	}

	return 0;
}

/**
 * @brief	remove a specific extended attribute
 *
 * @param[in]	pVnode	Vnode to remove extended attributes
 * @param[in]	dwID	id of namespace 
 * @param[in]	psName	name of extended attribute
 * @returns		0 on success, or a negative error number on failure
 */
int
XattrCommRemoveName(
	PVNODE			pVnode,
	XATTR_NAMESPACE_ID	dwID,
	const char*		psName)
{
	FERROR			nErr = FERROR_NO_ERROR;

	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* check operation */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnRemoveXAttribute))
	{
		LNX_CMZ(("No Native interface for removing xattr"));
		return -ENOSYS;
	}

	nErr = pVnode->pVnodeOps->pfnRemoveXAttribute(pVnode, psName, dwID);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS RemoveXattr fails(nErr: %08x)", -nErr));
		return RtlLinuxError(nErr);
	}

	return 0;
}


/******************************************************************************/
/* Functions for Linux VFS                                                    */
/******************************************************************************/

/**
 * @brief 		get extended attribute names for listxattr, or computed the buffer size required
 *
 * @param[in]	pDentry 	dentry having extended attributes
 * @param[out]	pBuffer		buffer that will contains names
 * @param[in]	szBufferSize the size of buffer
 * @returns		the number of bytes used / required on success, or a negative error number on failure
 *
 * If pBuffer is NULL, return the size of the buffer required.
 */
LINUX_SSIZE_T
XattrListNames(
	PLINUX_DENTRY pDentry,
	char *pBuffer,
	LINUX_SIZE_T szBufferSize)
{
	PVNODE			pVnode = NULL;
	FERROR			nErr = FERROR_NO_ERROR;
	unsigned int	dwSizeRead = 0;

	if (!pDentry || !pDentry->d_inode)
	{
		LNX_CMZ(("Invalid parameter"));
		RFS_ASSERT(0);
		return -EINVAL;
	}

	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	/* check operation */
	if ((NULL == pVnode->pVnodeOps) ||
			(NULL == pVnode->pVnodeOps->pfnListXAttributes))
	{
		LNX_CMZ(("No Native interface for listing xattr"));
		RFS_ASSERT(0);
		return -ENOSYS;
	}

	/* get NativeFS's xattr list */
	nErr = pVnode->pVnodeOps->pfnListXAttributes(pVnode,
												pBuffer,
												szBufferSize,
												&dwSizeRead);
	if (nErr != FERROR_NO_ERROR)
	{
		LNX_EMZ(("NativeFS ListXattr fails(nErr: %08x)", -nErr));
		return RtlLinuxError(nErr);
	}

	return dwSizeRead;
}

/*
 * Handler for extended attributes (user)
 */

/**
 * @brief get handler for user xattr
 *
 * @param[in]	pInode		inode to get value of user xattr
 * @param[in]	psName		name of xattr in user namespace
 * @param[out]	pValue		buffer to locate the value into
 * @param[in]	szValueSize	the size of buffer
 * @returns		the length of read value
 *
 * The Name doesn't have namespace such as "user.", but name of xattr such as "author".
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int 
_XattrUserGetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwHandlerFlags)
#else
static int 
_XattrUserGetValue(
	PLINUX_INODE	pInode,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize)
#endif
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	LNX_ASSERT_ARG(pDentry, -EINVAL);
#else
	LNX_ASSERT_ARG(pInode, -EINVAL);
#endif
	LNX_ASSERT_ARG(psName, -EINVAL);

	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
#else
	pVnode = VnodeGetVnodeFromInode(pInode);
#endif
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));

		RFS_ASSERT(0);
		return -EFAULT;
	}

	dwLinuxError = 
		XattrCommGetValue(pVnode, XATTR_ID_USER, psName, pValue, szValueSize);

	if (dwLinuxError < 0)
	{
		LNX_EMZ(("XattrCommGetValue (%s%s) fails (errno: %d)",
					LINUX_XATTR_USER_PREFIX, psName, dwLinuxError));
	}

	return dwLinuxError;
}

/**
 * @brief set handler for user xattr
 *
 * @param[in]	pInode		inode to set value of user xattr
 * @param[in]	psName		name of xattr in user namespace
 * @param[in]	pValue		value
 * @param[in]	szValueSize	the length of valid value
 * @returns		zero on success
 *
 * If pValue is NULL, remove the attribute.
 */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int
_XattrUserSetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags,
	int 			dwHandlerFlags)
#else
static int
_XattrUserSetValue(
	PLINUX_INODE	pInode,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
#endif
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	LNX_ASSERT_ARG(pDentry, -EINVAL);
#else
	LNX_ASSERT_ARG(pInode, -EINVAL);
#endif
	LNX_ASSERT_ARG(psName, -EINVAL);

	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
#else
	pVnode = VnodeGetVnodeFromInode(pInode);
#endif
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	if (pValue != NULL)
	{ 
		LNX_DMZ(("Set %s's Value", psName));

		/* set or change value of entry */
		dwLinuxError = XattrCommSetValue(pVnode, XATTR_ID_USER, psName,
				pValue, szValueSize, dwFlags);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("XattrCommSetValue (%s%s) fails (errno: %d)",
						LINUX_XATTR_USER_PREFIX, psName, dwLinuxError));
		}
	}
	else
	{
		LNX_DMZ(("Remove %s attribute", psName));

		/* remove entry if pValue is NULL */
		dwLinuxError = XattrCommRemoveName(pVnode, XATTR_ID_USER, psName);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("_XattrRemoveValue (%s%s) fails (errno: %d)",
						LINUX_XATTR_USER_PREFIX, psName, dwLinuxError));
		}
	}

	return dwLinuxError;
}

/*
 * Handler for extended attributes (trusted)
 */

/**
 * @brief get handler for trusted xattr
 *
 * @param[in]	pInode		inode to get value of trusted xattr
 * @param[in]	psName		name of xattr in trusted namespace
 * @param[out]	pValue		buffer to locate the value into
 * @param[in]	szValueSize	the size of buffer
 * @returns		the length of read value
 *
 * The Name doesn't have namespace such as "trusted.", but name of xattr such as "author".
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int 
_XattrTrustedGetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwHandlerFlags)
#else
static int 
_XattrTrustedGetValue(
	PLINUX_INODE	pInode,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize)
#endif
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	LNX_ASSERT_ARG(pDentry, -EINVAL);
#else
	LNX_ASSERT_ARG(pInode, -EINVAL);
#endif
	LNX_ASSERT_ARG(psName, -EINVAL);

	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
#else
	pVnode = VnodeGetVnodeFromInode(pInode);
#endif
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	dwLinuxError = 
		XattrCommGetValue(pVnode, XATTR_ID_TRUSTED, psName, pValue, szValueSize);

	if (dwLinuxError < 0)
	{
		LNX_EMZ(("XattrCommGetValue (%s%s) fails (errno: %d)",
					LINUX_XATTR_TRUSTED_PREFIX, psName, dwLinuxError));
	}

	return dwLinuxError;
}

/**
 * @brief set handler for trusted xattr
 *
 * @param[in]	pInode		inode to set value of trusted xattr
 * @param[in]	psName		name of xattr in trusted namespace
 * @param[in]	pValue		value
 * @param[in]	szValueSize	the length of valid value
 * @returns		zero on success
 *
 * If pValue is NULL, remove the attribute.
 */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int
_XattrTrustedSetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags,
	int 			dwHandlerFlags)
#else
static int
_XattrTrustedSetValue(
	PLINUX_INODE	pInode,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
#endif
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
#else
	pVnode = VnodeGetVnodeFromInode(pInode);
#endif
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Memory is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	if (pValue != NULL)
	{ 
		LNX_DMZ(("Set %s's Value", psName));

		dwLinuxError = XattrCommSetValue(pVnode, XATTR_ID_TRUSTED, psName,
				pValue, szValueSize, dwFlags);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("XattrCommSetValue (%s%s) fails (errno: %d)",
						LINUX_XATTR_TRUSTED_PREFIX, psName, dwLinuxError));
		}
	}
	else
	{
		LNX_DMZ(("Remove %s attribute", psName));

		/* remove entry if pValue is NULL */
		dwLinuxError = XattrCommRemoveName(pVnode, XATTR_ID_TRUSTED, psName);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("_XattrRemoveValue (%s%s) fails (errno: %d)",
						LINUX_XATTR_TRUSTED_PREFIX, psName, dwLinuxError));
		}
	}

	return dwLinuxError;
}


#ifdef CONFIG_RFS_FS_SECURITY
/*
 * Handler for extended attributes (security)
 */
/**
 * @brief get handler for security xattr
 *
 * @param[in]	pInode		inode to get value of security xattr
 * @param[in]	psName		name of xattr in security namespace
 * @param[out]	pValue		buffer to locate the value into
 * @param[in]	szValueSize	the size of buffer
 * @returns		the length of read value
 *
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int 
_XattrSecurityGetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwHandlerFlags)
#else
static int 
_XattrSecurityGetValue(
	PLINUX_INODE	pInode,
	const char*		psName,	/* without namespace */
	void*			pValue,
	LINUX_SIZE_T	szValueSize)
#endif
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	pVnode = VnodeGetVnodeFromInode(pDentry->d_inode);
#else
	pVnode = VnodeGetVnodeFromInode(pInode);
#endif
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Memory is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	dwLinuxError = 
		XattrCommGetValue(pVnode, XATTR_ID_SECURITY, psName, pValue, szValueSize);
	if (dwLinuxError < 0)
	{
		LNX_EMZ(("XattrCommGetValue (%s%s) fails (errno: %d)",
					LINUX_XATTR_SECURITY_PREFIX, psName, dwLinuxError));
	}

	return dwLinuxError;
}

static int xattr_set_value(PLINUX_INODE	pInode,
			   const char*		psName,
			   const void*		pValue,
			   LINUX_SIZE_T	szValueSize,
			   int 			dwFlags);


/**
 * @brief set handler for security xattr
 *
 * @param[in]	pInode		inode to set value of security xattr
 * @param[in]	psName		name of xattr in security namespace
 * @param[in]	pValue		value
 * @param[in]	szValueSize	the length of valid value
 * @returns		zero on success
 *
 * If pValue is NULL, remove the attribute.
 */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
static int
_XattrSecuritySetValue(
	PLINUX_DENTRY		pDentry,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags,
	int 			dwHandlerFlags)
#else
static int
_XattrSecuritySetValue(
	PLINUX_INODE	pInode,
	const char*		psName,		/* without namespace */
	const void*		pValue,		/* if null, remove xattr */
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
#endif
{
	if (!strcmp(psName, ""))
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	return xattr_set_value(pDentry->d_inode,
			       psName, pValue, szValueSize, dwFlags);
#else
	return xattr_set_value(pInode,
			       psName, pValue, szValueSize, dwFlags);
#endif
}


static int xattr_set_value(PLINUX_INODE	pInode,
			   const char*		psName,		/* without namespace */
			   const void*		pValue,		/* if null, remove xattr */
			   LINUX_SIZE_T	szValueSize,
			   int 			dwFlags)
{
	PVNODE	pVnode = NULL;
	int		dwLinuxError = 0;

	pVnode = VnodeGetVnodeFromInode(pInode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Memory is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	if (pValue != NULL)
	{ 
		LNX_DMZ(("Set %s's Value", psName));

		dwLinuxError = XattrCommSetValue(pVnode, XATTR_ID_SECURITY, psName,
				pValue, szValueSize, dwFlags);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("XattrCommSetValue (%s%s) fails (errno: %d)",
						LINUX_XATTR_SECURITY_PREFIX, psName, dwLinuxError));
		}
	}
	else
	{
		LNX_DMZ(("Remove %s attribute", psName));

		/* remove entry if pValue is NULL */
		dwLinuxError = XattrCommRemoveName(pVnode, XATTR_ID_SECURITY, psName);
		if (dwLinuxError < 0)
		{
			LNX_EMZ(("XattrCommRemoveName (%s%s) fails (errno: %d)",
						LINUX_XATTR_SECURITY_PREFIX, psName, dwLinuxError));
		}
	}

	return dwLinuxError;
}

int rfs_initxattrs(struct inode *inode, const struct xattr *xattr_array,
                   void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = xattr_set_value(inode, xattr->name, xattr->value,
				      xattr->value_len, 0);
		if (err < 0)
			break;
	}
        return err;
}

/**
 * @brief	initialize security xattr of new inode. called from VnodeInitialize
 * 
 * @param[in]	pInode
 * @param[in]	pParentInode
 * @return		zero on success or negative errno
 *
 * This operation should be called while initializing new inode
 */
int
XattrInitSecurity(
	PLINUX_INODE	pInode,
	PLINUX_INODE	pParentInode,
	const struct qstr *qstr)
{
	return LINUX_SecurityInodeInit(pInode, pParentInode, qstr,
				       &rfs_initxattrs, NULL);
}
#endif 	//CONFIG_RFS_FS_SECURITY

/******************************************************************************/
/* Handlers for Linux VFS                                                     */
/******************************************************************************/
/*
 * handlers for user namespace
 */
LINUX_XATTR_HANDLER	stXattrUserHandler =
{
	.prefix	= LINUX_XATTR_USER_PREFIX,
	.set	= _XattrUserSetValue,
	.get	= _XattrUserGetValue,
	.list	= NULL,
};

/*
 * handlers for trusted namespace
 */
LINUX_XATTR_HANDLER	stXattrTrustedHandler =
{
	.prefix	= LINUX_XATTR_TRUSTED_PREFIX,
	.set	= _XattrTrustedSetValue,
	.get	= _XattrTrustedGetValue,
	.list	= NULL,
};

#ifdef CONFIG_RFS_FS_SECURITY
/*
 * handlers for security namespace
 */
LINUX_XATTR_HANDLER	stXattrSecurityHandler =
{
	.prefix	= LINUX_XATTR_SECURITY_PREFIX,
	.set	= _XattrSecuritySetValue,
	.get	= _XattrSecurityGetValue,
	.list	= NULL,
};
#endif

/*
 * xattr handler table
 *  : when initializing superblock, this table is mapped to superblock
 */
const struct xattr_handler *g_stLinuxXattrHandlers[] =
{
	&stXattrUserHandler,
	&stXattrTrustedHandler,
# ifdef CONFIG_RFS_FS_ACL
	&stXattrAclAccessHandler,
	&stXattrAclDefaultHandler,
# endif
# ifdef CONFIG_RFS_FS_SECURITY
	&stXattrSecurityHandler,
# endif
	NULL,
};

// end of file
