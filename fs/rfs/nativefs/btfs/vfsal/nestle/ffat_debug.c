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
* @file		nsd_debug.c
* @brief	Define debug MACROs and functions
* @author	Kwon Moon Sang
* @date		2008/08/26
* @remark		
* @version	RFS_3.0.0_b047_RTM
* @note
*			26-AUG-2008 [Moonsang Kwon]: First writing
*			19-SEP-2008 [Moonsang Kwon]: Add new functions
*				1. _NsdGetLastError() returns the last error number.
*				2. _NsdGetThreadId() returns current thread's id.
*				3. _NsdLockConsole() gets lock for the console.
*				4. _NsdUnlockConsole() releases lock for the console.
*/

#include "ffat_debug.h"
#include "ns_oam_defs.h"

#if defined(__arm)
	#include <errno.h>
#endif

#if (ENABLE_LINUX_KERNEL >= 1)
#else
	#include <stdio.h>
#endif

///< CONF: Set default Debug Zone Mask
static volatile unsigned int
gnBTFS_DebugZoneMask = BTFS_DZM_CORE | BTFS_DZM_FFATFS | BTFS_DZM_ADDON | BTFS_DZM_GLOBAL | BTFS_DZM_VFSAL;

/**
 * Return current global debug zone mask for this package.
 * @return	Return current NSD's debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark	This mask is used to determine whether to show the debug
 *			message or not.
 */
unsigned int
_BtfsGetDebugZoneMask(void)
{
	// return current debug zone mask
	return gnBTFS_DebugZoneMask;
}

/**
 * Set current global debug zone mask.
 * @param	[in] nNewNsdDebugZoneMask New NSD debug zone mask
 * @return	Return NSD's new debug zone mask
 * @author	Kwon Moon Sang
 * @version 2008/08/29 Initial version
 * @remark
 */
unsigned int
_BtfsSetDebugZoneMask(unsigned int nNewBtfsDebugZoneMask)
{
	// replace and return global debug zone mask
	gnBTFS_DebugZoneMask = (nNewBtfsDebugZoneMask);
	return gnBTFS_DebugZoneMask;
}

