/**
****************************************************************************************************
* @file SfdPlatformInterface.h
* @brief Security framework [SF] filter driver [D] platform interfaces implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_PLATFORM_INTERFACE_H_
#define _SFD_PLATFORM_INTERFACE_H_

#include <uapi/linux/sf/core/SfCore.h>
#include <linux/pm.h>

/**
****************************************************************************************************
* @brief Create, register and initialize platform interfaces to handle system events such sleep,
* 		 resume, shutdown, etc.
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCreatePlatformInterface(void);

/**
****************************************************************************************************
* @brief Destroy platform interfaces and free all allocated resources.
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdDestroyPlatformInterface(void);

/*
****************************************************************************************************
* PRIVATE FUNCTIONS
****************************************************************************************************
*/

struct platform_device;

/**
****************************************************************************************************
* @brief Handle suspend system event when it occures.
* @warning Private function. It is allowed to use only in SfdPlatformInterface.c
*
* @return 0 on success, -1 otherwise
****************************************************************************************************
*/
int SfdSuspendHandler(struct platform_device* pd, pm_message_t state);

/**
****************************************************************************************************
* @brief Handle resume system event when it occures.
* @warning Private function. It is allowed to use only in SfdPlatformInterface.c
*
* @return 0 on success, -1 otherwise
****************************************************************************************************
*/
int SfdResumeHandler(struct platform_device* pd);

/**
****************************************************************************************************
* @brief Handle remove system event when it occures.
* @warning Private function. It is allowed to use only in SfdPlatformInterface.c
*
* @return 0 on success, -1 otherwise
****************************************************************************************************
*/
int SfdRemoveHandler(struct platform_device* pd);

/**
****************************************************************************************************
* @brief Handle shutdown system event when it occures.
* @warning Private function. It is allowed to use only in SfdPlatformInterface.c
*
* @return void
****************************************************************************************************
*/
void SfdShutdownHandler(struct platform_device* pd);

#endif	/* !_SFD_PLATFORM_INTERFACE_H_ */
