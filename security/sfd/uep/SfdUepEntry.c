/**
****************************************************************************************************
* @file SfdUepEntry.c
* @brief Security framework [SF] filter driver [D] Unauthorized Execution Prevention [UEP] module
*	entry point
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 9, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

//	system includes
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/version.h>

#include "SfdUep.h"

/**
* @brief Notifier module context
*/
static SfdUepContext s_uepContext;

/**
****************************************************************************************************
* @brief Called when insmod executing. Allocate necessary resources and create user-kernel 
* intergfaces.
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static int __init SfdUepModuleInit(void)
{
	return SfdOpenUepContext(&s_uepContext);
}

/**
****************************************************************************************************
* @brief Called when insmod executing. Allocate necessary resources and create user-kernel 
* intergfaces.
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static void __exit SfdUepModuleExit(void)
{
	SfdCloseUepContext(&s_uepContext);
}

/*
****************************************************************************************************
* MODULE OBLIGATORY DECLARATIONS
****************************************************************************************************
*/
module_init(SfdUepModuleInit);
module_exit(SfdUepModuleExit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maksym Koshel (m.koshel@samsung.com)");
MODULE_VERSION("v.0.0.1");
