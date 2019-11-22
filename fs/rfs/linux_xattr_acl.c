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
 * @file        linux_xattr_acl.c
 * @brief       This file includes extended attribute operations for posix_acl.
 * @version     RFS_3.0.0_b047_RTM
 * @see         none
 * @author      hayeong.kim@samsung.com
 */

#include "linux_xattr.h"
#include "linux_vnode.h"

#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>

#undef RFS_FILE_ZONE_MASK
#define RFS_FILE_ZONE_MASK		(eRFS_DZM_XATTR)

/******************************************************************************/
/* Internal Functions                                                         */
/******************************************************************************/
/**
 * @brief convert xattr to posix_acl struct and check validity
 * @param	pValue			xattr value
 * @param	szValueSize		size of buffer holding xattr value
 * @param	ppstPosixAcl	address of posix_acl buffer
 * @return zero on success
 */
static LINUX_ERROR
_XattrCheckAclValue(
	const void 			*pValue,
	LINUX_SIZE_T 		szValueSize,
	PLINUX_POSIX_ACL	*ppstPosixAcl)
{
	PLINUX_POSIX_ACL	pstAcl = NULL;
	LINUX_ERROR			dwLinuxError = 0;

	LNX_ASSERT_ARG(ppstPosixAcl, -EINVAL);

	/* if value is NULL, remove attributes */
	if (pValue == NULL)
	{
		*ppstPosixAcl = NULL;
		return dwLinuxError;
	}

	/* convert pValue (in little endian) to struct posix_acl */
	pstAcl = LINUX_PosixAclFromXAttr(pValue, szValueSize);
	if (!IS_ERR(pstAcl))
	{
		/* check the validity of value as posix_acl */
		dwLinuxError = LINUX_PosixAclValid(pstAcl);

		*ppstPosixAcl = pstAcl;
	}
	else
	{
		/* set errno */
		dwLinuxError = PTR_ERR(pstAcl);
		*ppstPosixAcl = NULL;
	}

	/*
	 * We don't need to convert the value of posix_acl to little endian.
	 * Because each field in pValue is already stored in little endian.
	 */

	/* if success, return 0, or errno */
	return dwLinuxError;
}

/**
 * @brief apply new Acl to Inode(vnode) in memory
 * @param pVnode	vnode
 * @param dwID		ID for xattr namespace
 * @param pstNewAcl	posix_acl struct to be applied to vnode
 * @return
 */
static void
_XattrSetAcl(
	PVNODE				pVnode,
	XATTR_NAMESPACE_ID	dwID,
	PLINUX_POSIX_ACL	pstNewAcl)
{
	PLINUX_INODE	pInode = NULL;
	PLINUX_POSIX_ACL *ppstCurAcl = NULL;

	LNX_ASSERT_ARGV(pVnode);
	LNX_ASSERT_ARGV(pstNewAcl);

	pInode = &pVnode->stLxInode;

	/* get addr of current posix_acl of vnode */
	if (dwID == XATTR_ID_POSIX_ACL_ACCESS)
	{
		ppstCurAcl = &pVnode->pstLxAcl;
	}
	else if (dwID == XATTR_ID_POSIX_ACL_DEFAULT)
	{
		ppstCurAcl = &pVnode->pstLxAclDefault;
	}

	/* protect inode */
	LINUX_SpinLock(&pInode->i_lock);

	/* release old acl */
	if (*ppstCurAcl)
	{
		LINUX_PosixAclRelease(*ppstCurAcl);
	}

	/* apply new acl to Vnode */
	*ppstCurAcl = LINUX_PosixAclDup(pstNewAcl);

	LINUX_SpinUnlock(&pInode->i_lock);

	return;
}

/**
 * @brief get posix_acl of inode
 * @param pVnode	vnode
 * @param dwID		ID for xattr namespace
 * @return address of posix_acl of inode
 */
static PLINUX_POSIX_ACL
_XattrGetAcl(
	PVNODE				pVnode,
	XATTR_NAMESPACE_ID	dwID)
{
	PLINUX_INODE	pInode = NULL;
	PLINUX_POSIX_ACL pstPosixAcl = NULL;

	LNX_ASSERT_ARG(pVnode, NULL);

	pInode = &pVnode->stLxInode;
	if (NULL == pInode)
	{
		LNX_CMZ(("inode is not initialized"));
		RFS_ASSERT(0);
		return NULL;
	}

	/* protect inode */
	LINUX_SpinLock(&pInode->i_lock);

	/* get posix inode of inode */
	if (dwID == XATTR_ID_POSIX_ACL_ACCESS)
	{
		pstPosixAcl = LINUX_PosixAclDup(pVnode->pstLxAcl);
	}
	else if (dwID == XATTR_ID_POSIX_ACL_DEFAULT)
	{
		pstPosixAcl = LINUX_PosixAclDup(pVnode->pstLxAclDefault);
	}

	LINUX_SpinUnlock(&pInode->i_lock);

	return pstPosixAcl;
}

/**
 * @brief change inode's permission (i_mode) to apply posix_acl
 *
 * @param pVnode		vnode
 * @param dwID			ID for xattr namespace
 * @param pstPosixAcl	posix_acl to apply
 * 
 * @return Returns 0 if the acl can be exactly represented in the file mode permission bits (i_mode), or else 1. Returns negative errno on error
 */
static LINUX_ERROR
_XattrSetAclPerm(
	PVNODE				pVnode,
	XATTR_NAMESPACE_ID	dwID,
	PLINUX_POSIX_ACL	pstPosixAcl)
{
	LINUX_ERROR		dwLinuxError = 0;

	LNX_ASSERT_ARG(pVnode, -EINVAL);

	/* not change */
	if (NULL == pstPosixAcl)
		return dwLinuxError;

	/* apply new posix_acl to i_mode */
	if (dwID == XATTR_ID_POSIX_ACL_ACCESS)
	{
		LINUX_MODE wIMode = pVnode->stLxInode.i_mode;

		/*
		 * change posix_acl to i_mode
		 *
		 * Returns 0 if the acl can be exactly represented in the traditional
		 * file mode permission bits, or else 1.
		 * So, if zero is returned, do not need to update xattr.
		 */
		dwLinuxError = LINUX_PosixAclEquivMode(pstPosixAcl, &wIMode);
		if (dwLinuxError < 0)
		{
			LNX_SMZ(("Invalid ACL tag for converting to i_mode (errno: %d)",
						dwLinuxError));

			/* invalid value */
			goto out;
		}
		else
		{
			FERROR nErr = FERROR_NO_ERROR;

			/* update new i_mode converted from posix_acl */
			pVnode->stLxInode.i_mode = wIMode;

			/* check operation */
			if ((NULL == pVnode->pVnodeOps) ||
					(NULL == pVnode->pVnodeOps->pfnSetGuidMode))
			{
				LNX_CMZ(("No Native interface for change mode"));
				dwLinuxError = -ENOSYS;
				RFS_ASSERT(0);
				goto out;
			}

			/* notify nativeFS the change of mode(permission) */
			nErr = pVnode->pVnodeOps->pfnSetGuidMode(pVnode,
										pVnode->stLxInode.i_uid,
										pVnode->stLxInode.i_gid,
										(wIMode & LINUX_S_IALLUGO));
			if (nErr != FERROR_NO_ERROR)
			{
				LNX_EMZ(("NativeFS GetGuidMode fails (nErr : 0x%x)", -nErr));

				dwLinuxError = RtlLinuxError(nErr);
				goto out;
			}
			else
			{
				LINUX_MarkInodeDirty(&pVnode->stLxInode);
			}
		}
	}
	else if (dwID == XATTR_ID_POSIX_ACL_DEFAULT)
	{
		if (!LINUX_IS_DIR(pVnode->stLxInode.i_mode))
		{
			dwLinuxError = (pstPosixAcl)? -EACCES : 0;
			return dwLinuxError;
		}
	}
	else
	{
		LNX_CMZ(("Invalid Xattr ID(%d)", dwID));
		dwLinuxError = -EINVAL;
	}

out:
	return dwLinuxError;
}

/**
 * @brief apply xattr value to posix_acl and vnode, and write xattr to disk
 *
 * @param pVnode		vnode
 * @param dwID			ID for xattr namespace
 * @param psName		attribute name
 * @param pValue		attribute value
 * @param szValueSize	size of buffer
 * @param dwFlags		flags to set xattr
 * @return zero on success
 *
 * 1. check for the validation of pValue
 * 	1-1. convert xattr value -> posix_acl
 * 	1-2. verify that each field in posix is proper
 * 2. apply posix_acl value to i_mode of inode (notify Native)
 * 3. apply posix_acl to inode's acl field
 * 4. set xattr for acl (notify Native)
 */
static int
_XattrACLSetInternal(
	PVNODE			pVnode,
	XATTR_NAMESPACE_ID	dwID,
	const char*		psName,
	const void*		pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
{
	LINUX_ERROR			dwLinuxError = 0;
	PLINUX_POSIX_ACL	pstPosixAcl = NULL;

	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(psName, -EINVAL);

	/* check xattr ID */
	if ((dwID != XATTR_ID_POSIX_ACL_ACCESS) &&
			(dwID != XATTR_ID_POSIX_ACL_DEFAULT))
	{
		/* invalid ID */
		LNX_DMZ(("Invalid Namespace ID(%d)", dwID));

		return -EINVAL;
	}

	/*
	 * create posix_acl from pValue and check the validation
	 * if pValue is NULL, pstPosixAcl is NULL -> remove Acl attribute
	 */
	dwLinuxError = _XattrCheckAclValue(pValue, szValueSize, &pstPosixAcl);
	if (0 != dwLinuxError)
	{ 
		LNX_EMZ(("ACL value is invalid(errno: %d)", dwLinuxError));

		goto error;
	}

	/*
	 * apply posix_acl to current inode's i_mode
	 * It changes i_mode and calls Native's function for changing permission
	 * If posix_acl don't need xattr, return 0 or 1.
	 * If the return valus is 0, remove ACL attribute
	 */
	dwLinuxError = _XattrSetAclPerm(pVnode, dwID, pstPosixAcl);
	if (dwLinuxError < 0)
	{
		LNX_EMZ(("Changing Inode's Permission fails (errno: %d)", dwLinuxError));

		/*
		 * if pstPosixAcl is not NULL,
		 * decrease reference count and free memory
		 */
		goto error;
	}

	/* remove entry if pValue is NULL or posix_acl don't need xattr */
	if ((0 == dwLinuxError) || (NULL == pValue))
	{
		LNX_DMZ(("Remove %s attr", psName));

		dwLinuxError = XattrCommRemoveName(pVnode, dwID, psName); 
	}
	else
	/* store xattr value to Native */
	{
		LNX_DMZ(("Set %s's value", psName));

		dwLinuxError = XattrCommSetValue(pVnode,
								dwID,
								psName,
								pValue,
								szValueSize,
								dwFlags);
	}

	/* if failure, release posixAcl */
	if (0 != dwLinuxError)
	{
		LNX_EMZ(("Changing %s fails", psName));
		goto error;
	}

	/* apply new Acl to Inode */
	_XattrSetAcl(pVnode, dwID, pstPosixAcl);

	LNX_DMZ(("Successfully set new Acl to Inode and save as Xattr"));
	
	/* if success, return 0 */
	return dwLinuxError;

error:
	if (pstPosixAcl)
		LINUX_PosixAclRelease(pstPosixAcl);
	return dwLinuxError;
}

/**
 * @brief read xattr from disk and apply xattr value to posix_acl and vnode
 *
 * @param pVnode		vnode
 * @param dwID			ID for xattr namespace
 * @param psName		attribute name
 * @param pValue		attribute value
 * @param szValueSize	size of buffer
 * @return zero on success
 *
 * 1. get posix_acl from inode
 * 2. convert posix_acl -> xattr value (little endian)
 * 3. get xattr value (get from Native)
 * 4. if acl and xattr is not the same, apply xattr to acl
 */
static int
_XattrACLGetInternal(
	PVNODE			pVnode,
	XATTR_NAMESPACE_ID	dwID,
	const char*		psName,
	void*		pValue,
	LINUX_SIZE_T	szValueSize)
{
	PLINUX_POSIX_ACL	pstPosixAcl = NULL;
	int					szReadSize = 0;	

	LNX_ASSERT_ARG(pVnode, -EINVAL);
	LNX_ASSERT_ARG(psName, -EINVAL);

	/* check id */
	if ((dwID != XATTR_ID_POSIX_ACL_ACCESS) &&
			(dwID != XATTR_ID_POSIX_ACL_DEFAULT))
	{
		/* invalid ID */
		LNX_DMZ(("Invalid Namespace ID(%d)", dwID));

		return -EINVAL;
	}

	/* get posix_acl from inode */
	pstPosixAcl = _XattrGetAcl(pVnode, dwID);
	if (pstPosixAcl != NULL)
	{
		/* convert posix_acl to xattr value */
		szReadSize = LINUX_PosixAclToXAttr(pstPosixAcl, pValue, szValueSize);

		/* release acl because do not need it anymore */
		LINUX_PosixAclRelease(pstPosixAcl);
	}
	else
	/* get xattr value from device(NativeFS) */
	{
		LINUX_ERROR			dwLinuxError = 0;
		PLINUX_POSIX_ACL	pstPosixAcl = NULL;

		/* get xattr value from Native */
		szReadSize = XattrCommGetValue(pVnode, dwID, psName, pValue, szValueSize);
		if (szReadSize < 0)
		{
			/* return errno */
			LNX_EMZ(("XattrGetValue(%s) fails (errno: %d)", psName, szReadSize));
			goto out;
		}

		/* convert xattr value to posix_acl */
		dwLinuxError = _XattrCheckAclValue(pValue, szValueSize, &pstPosixAcl);
		if (0 != dwLinuxError)
		{ 
			LNX_EMZ(("ACL value is invalid(errno: %d)", dwLinuxError));

			goto out;
		}

		/* initialize posix_acl to vnode */
		_XattrSetAcl(pVnode, dwID, pstPosixAcl);
	}

out:
	return szReadSize;

}
/******************************************************************************/
/* Functions for Linux VFS                                                    */
/******************************************************************************/

/**
 * @brief set handler for ACL default xattr
 * 
 * @param[in]	pInode		inode to set value of acl xattr
 * @param[in]	psName		name of xattr in acl namespace
 * @param[in]	pValue		value
 * @param[in]	szValueSize	the length of valid value
 * @returns		zero on success
 *
 * If pValue is NULL, remove the attribute.
 */
static int
_XattrACLSetDefault(
	PLINUX_INODE	pInode,
	const char*		psName,
	const void*		pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
{
	PVNODE				pVnode = NULL;

	if ((NULL == pInode) || (NULL == psName))
	{
		LNX_CMZ(("Invalid parameter(null)"));
		RFS_ASSERT(0);
		return -EINVAL;
	}

	/* psName should be NULL string("") in ACL */
	if (strcmp(psName, "") != 0)
	{
		/* invalid ACL attribute */
		LNX_EMZ(("Invalid name for ACL(%s%s)",
				LINUX_POSIX_ACL_XATTR_DEFAULT, psName));
		return -EINVAL;
	}

	/* not support for symlink */
	if (LINUX_IS_LNK(pInode->i_mode))
	{
		LNX_EMZ(("Not support Acl for symbolic link"));
		return -EOPNOTSUPP;	/* not support (FIXME any other errno? -EPERM?) */
	}

	/* convert inode -> vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	return _XattrACLSetInternal(pVnode, XATTR_ID_POSIX_ACL_DEFAULT,
								psName, pValue, szValueSize, dwFlags);
}

/**
 * @brief get handler for ACL default xattr
 *
 * @param[in]	pInode		inode to get value of acl xattr
 * @param[in]	psName		name of xattr in acl namespace
 * @param[out]	pValue		buffer to locate the value into
 * @param[in]	szValueSize	the size of buffer
 * @returns		the length of read value
 */
static int 
_XattrACLGetDefault(
	PLINUX_INODE	pInode,
	const char*		psName,
	void*			pValue,
	LINUX_SIZE_T	szValueSize)
{
	PVNODE	pVnode = NULL;

	if ((NULL == pInode) || (NULL == psName))
	{
		LNX_CMZ(("Invalid parameter(null)"));
		return -EINVAL;
	}

	/* invalid ACL attribute */
	if (strcmp(psName, "") != 0)
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

	/* convert inode -> vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	return _XattrACLGetInternal(pVnode, XATTR_ID_POSIX_ACL_DEFAULT,
								psName, pValue, szValueSize);
}

/**
 * @brief set handler for ACL access xattr
 * 
 * @param[in]	pInode		inode to set value of acl xattr
 * @param[in]	psName		name of xattr in acl namespace
 * @param[in]	pValue		value
 * @param[in]	szValueSize	the length of valid value
 * @returns		zero on success
 *
 * If pValue is NULL, remove the attribute.
 */
static int
_XattrACLSetAccess(
	PLINUX_INODE	pInode,
	const char*		psName,
	const void*		pValue,
	LINUX_SIZE_T	szValueSize,
	int 			dwFlags)
{
	PVNODE				pVnode = NULL;

	if ((NULL == pInode) || (NULL == psName))
	{
		LNX_CMZ(("Invalid parameter(null)"));
		return -EINVAL;
	}

	/* psName should be NULL string("") in ACL */
	if (strcmp(psName, "") != 0)
	{
		/* invalid ACL attribute */
		LNX_EMZ(("Invalid name for ACL(%s%s)",
				LINUX_POSIX_ACL_XATTR_ACCESS, psName));
		return -EINVAL;
	}

	/* not support for symlink */
	if (LINUX_IS_LNK(pInode->i_mode))
	{
		LNX_EMZ(("Not support Acl for symbolic link"));
		return -EOPNOTSUPP;	/* not support (FIXME any other errno? -EPERM?) */
	}

	/* convert inode -> vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	return _XattrACLSetInternal(pVnode, XATTR_ID_POSIX_ACL_ACCESS,
								psName, pValue, szValueSize, dwFlags);
}

/**
 * @brief get handler for ACL access xattr
 *
 * @param[in]	pInode		inode to get value of acl xattr
 * @param[in]	psName		name of xattr in acl namespace
 * @param[out]	pValue		buffer to locate the value into
 * @param[in]	szValueSize	the size of buffer
 * @returns		the length of read value
 */
static int 
_XattrACLGetAccess(
	PLINUX_INODE	pInode,
	const char*		psName,
	void*			pValue,
	LINUX_SIZE_T	szValueSize)
{
	PVNODE	pVnode = NULL;

	if ((NULL == pInode) || (NULL == psName))
	{
		LNX_CMZ(("Invalid parameter(null)"));
		return -EINVAL;
	}

	/* invalid ACL attribute */
	if (strcmp(psName, "") != 0)
	{
		LNX_DMZ(("Name is NULL string"));
		return -EINVAL;
	}

	/* convert inode -> vnode */
	pVnode = VnodeGetVnodeFromInode(pInode);
	if (unlikely(NULL == pVnode))
	{
		LNX_CMZ(("Vnode address is broken"));
		RFS_ASSERT(0);
		return -EFAULT;
	}

	return _XattrACLGetInternal(pVnode, XATTR_ID_POSIX_ACL_ACCESS,
								psName, pValue, szValueSize);
}

/******************************************************************************/
/* Handlers for Linux VFS                                                     */
/******************************************************************************/
/*
 * handlers for default ACL namespace
 */
LINUX_XATTR_HANDLER	stXattrAclDefaultHandler =
{
	.prefix = LINUX_POSIX_ACL_XATTR_DEFAULT,
	.set	= _XattrACLSetDefault,
	.get	= _XattrACLGetDefault,
	.list	= NULL,
};

/*
 * handlers for access ACL namespace
 */
LINUX_XATTR_HANDLER	stXattrAclAccessHandler =
{
	.prefix = LINUX_POSIX_ACL_XATTR_ACCESS,
	.set	= _XattrACLSetAccess,
	.get	= _XattrACLGetAccess,
	.list	= NULL,
};

// end of file
