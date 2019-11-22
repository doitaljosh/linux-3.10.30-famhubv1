/**
 *     @mainpage   Nestle Layer
 *
 *     @section Intro
 *       TFS5 File system's VFS framework
 *   
 *     @MULTI_BEGIN@ @COPYRIGHT_DEFAULT
 *     @section Copyright COPYRIGHT_DEFAULT
 *            COPYRIGHT. SAMSUNG ELECTRONICS CO., LTD.
 *                                    ALL RIGHTS RESERVED
 *     Permission is hereby granted to licensees of Samsung Electronics Co., Ltd. products
 *     to use this computer program only in accordance 
 *     with the terms of the SAMSUNG FLASH MEMORY DRIVER SOFTWARE LICENSE AGREEMENT.
 *     @MULTI_END@
 *
 */

/** 
* @file		nsd_version.c
* @brief	Define Version-related MACROs and Type
* @author	Kwon Moon Sang
* @date		2008/08/26
* @remark	Define Samsung Sung Package Version Information structure type.
* @version	RFS_3.0.0_b047_RTM
* @note
*			26-AUG-2008 [Moonsang Kwon]: First writing
*/

#include "nsd_base.h"
#include "nsd_debug.h"

#if (ENABLE_LINUX_KERNEL >= 1)
	#include <linux/types.h>
#else
	#include <stdio.h>
#endif

#if defined(__arm) || (ENABLE_LINUX_KERNEL >= 1) || (ENABLE_SYMBIAN == 1)
#else
#include <wchar.h>
#endif

// set file debug zone
#define NSD_FILE_ZONE_MASK	(eNSD_DZM_TEST | eNSD_DZM_INIT)

// Use for your purpose
static char*	_pszNsdVersion = NSD_VERSION_STRING; 
static wchar_t*	_pswzNsdVersion = (wchar_t*) (T2W(NSD_VERSION_STRING));

// define package version information string and structure
static NSD_VERSION _stNsdVersion = 
{
	NSD_VERSION_MAJOR,
	NSD_VERSION_MIDDLE,
	NSD_VERSION_MINOR,
	NSD_VERSION_PATCH,
	NSD_VERSION_BUILD,
	TEXT(NSD_NAME_STRING_FULL),	// Package's full name
};

/**
 * @brief return package version information string and structure
 * @param ppszVersion: place to store version string in OEM code
 * @param ppswzVersion: place to store version string in Unicode
 * @param ppNsdVersion: place to store version information
 * @returns
 * @remarks 
 * @see 
 * 
  */
void
NsdGetVersion(char** ppszVersion, wchar_t** ppswzVersion, PPNSD_VERSION ppNsdVersion)
{
	NSD_IMZC(1, (_T("NSD_VERSION size = %d byte."), sizeof(NSD_VERSION)));

	if (NULL != ppszVersion)
	{
		// return OEM version string
		*ppszVersion = _pszNsdVersion;
	}

	if (NULL != ppswzVersion)
	{
		// return Unicode version string
		*ppswzVersion = _pswzNsdVersion;
	}

	if (NULL != ppNsdVersion)
	{
		// return version information structure
		*ppNsdVersion = &_stNsdVersion;
	}
}



