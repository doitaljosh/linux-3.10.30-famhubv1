/**
****************************************************************************************************
* @file SfdEntry.c
* @brief Security framework [SF] filter driver [D] entry point
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

#include "Sfd.h"

#include <linux/module.h>
#include <linux/version.h>

/**
****************************************************************************************************
* @brief Main Security Filter Driver context. This structure map all kernel related mechanism of the
*	Smart Security subsystem.
****************************************************************************************************
*/
static SfdContext s_sfdContext;

/**
****************************************************************************************************
* @brief Called on kernel seuciryt subsytem initialization. Opening SFD context and switch execution
*	to main SFD routine.
*
* @return SF_STATUS_OK (equals to 0) on success, SF_STATUS_FAIL (equals to -1) otherwise
****************************************************************************************************
*/
static int __init SfdModuleInit(void)
{
    return SfdOpenContext( &s_sfdContext );
}

/**
****************************************************************************************************
* @brief __initcall used instead of seuciryt_initcall since Security Framework uses netlink socket
*	for communication and it can not be initialized on early init.
****************************************************************************************************
*/
__initcall(SfdModuleInit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maksym Koshel (m.koshel@samsung.com)");
MODULE_AUTHOR("Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)");
MODULE_VERSION("v.1.0.1");
MODULE_DESCRIPTION("Security Filter Driver");