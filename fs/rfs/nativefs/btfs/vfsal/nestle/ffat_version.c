/**
*   @section Intro Intro
*	This project is to provide a programming base so that we can use it as a 
*	starting point of programming.
*    
*	@section  Copyright    Copyright
*            COPYRIGHT. 2008 SAMSUNG ELECTRONICS CO., LTD.                
*                            ALL RIGHTS RESERVED                              
*                                                                            
*	Permission is hereby granted to licensees of Samsung Electronics Co., Ltd. 
*	products to use or abstract this computer program only in  accordance with 
*	the terms of the NAND FLASH MEMORY SOFTWARE LICENSE AGREEMENT for the sole 
*	purpose of implementing  a product based  on Samsung Electronics Co., Ltd. 
*	products. No other rights to  reproduce, use, or disseminate this computer 
*	program, whether in part or in whole, are granted.                         
*                                                                            
*	Samsung Electronics Co., Ltd. makes no  representation or warranties  with 
*	respect to  the performance  of  this computer  program,  and specifically 
*	disclaims any  responsibility for  any damages,  special or consequential, 
*	connected with the use of this program.                                    
*                                                                            
*	@section Description Description
*	This project is an experimental project to develop a programming base for 
*	allprojects in Flash S/W Group of Samsung Electronics.
*	We will define the basic debugging MACROs, version information, and types.
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
#include "ffat_debug.h"
#include "ffat_version.h"

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
#define BTFS_FILE_ZONE_MASK		(eBTFS_DZM_ALL)

// Use for your purpose
static char*	_pszBtfsVersion = BTFS_VERSION_STRING;
static wchar_t*	_pswzBtfsVersion = (wchar_t*)(T2W(BTFS_VERSION_STRING));

// define package version information string and structure
static BTFS_VERSION _stBtfsVersion =
{
	BTFS_VERSION_MAJOR,
	BTFS_VERSION_MIDDLE,
	BTFS_VERSION_MINOR,
	BTFS_VERSION_PATCH,
	BTFS_VERSION_BUILD,
	TEXT(BTFS_NAME_STRING_FULL),	// Package's full name
};


/**
 * @brief return package version information string and structure
 * @param ppszVersion: place to store version string in OEM code
 * @param ppswzVersion: place to store version string in Unicode
 * @param ppBtfsVersion: place to store version information
 * @returns
 * @remarks 
 * @see 
 * 
  */
void
BtfsGetVersion(char** ppszVersion, wchar_t** ppswzVersion, PPBTFS_VERSION ppBtfsVersion)
{
	if (NULL != ppszVersion)
	{
		// return OEM version string
		*ppszVersion = _pszBtfsVersion;
	}

	if (NULL != ppswzVersion)
	{
		// return Unicode version string
		*ppswzVersion = _pswzBtfsVersion;
	}

	if (NULL != ppBtfsVersion)
	{
		// return version information structure
		*ppBtfsVersion = &_stBtfsVersion;
	}
}

