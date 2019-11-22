/**
****************************************************************************************************
* @file SfdPlatformInterface.c
* @brief Security framework [SF] filter driver [D] platform interfaces implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdPlatformInterface.h"
#include <linux/platform_device.h>

/**
***************************************************************************************************
* @brief Name of the kernel module that will be registered as identifier of the platrom interface
*		 structure.
***************************************************************************************************
*/
#define SFD_PLATFORM_DEVICE_NAME 	"SFD"

/**
***************************************************************************************************
* @brief Uniqe device ID registered as platform interface.
***************************************************************************************************
*/
#define SFD_PLATFORM_DEVICE_NUMBER	0xc001c001

/**
* @brief Initialization platform_driver structure. This defined in the linux kernel sources.
*/
static struct platform_driver s_platformDriver =
{
	.suspend 	= SfdSuspendHandler,
	.resume 	= SfdResumeHandler,
	.shutdown 	= SfdShutdownHandler,
	.remove 	= SfdRemoveHandler,
	.driver =
	{
		.name   = SFD_PLATFORM_DEVICE_NAME,
		.owner  = THIS_MODULE,
	},
};

/**
* Pointer to the platform device that is used to control plaform
* interfaces registered in the system.
*/
static struct platform_device* s_pPlatformDevice = NULL;

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCreatePlatformInterface(void)
{
	SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;

	do
	{
		s_pPlatformDevice = platform_device_alloc(SFD_PLATFORM_DEVICE_NAME,
			SFD_PLATFORM_DEVICE_NUMBER);
		if (NULL == s_pPlatformDevice)
		{
			SF_LOG_E("Can't allocate platform device");
			break;
		}

		result = platform_device_add(s_pPlatformDevice);
		if (0 != result)
		{
			SF_LOG_E("Can't add platform device");
			break;
		}

		result = platform_driver_register(&s_platformDriver);
		if (0 != result)
		{
			SF_LOG_E("Can't register platform driver");
			break;
		}

		result = SF_STATUS_OK;

	} while(FALSE);
	
	if (SF_FAILED(result))
	{
		platform_device_del(s_pPlatformDevice);
		s_pPlatformDevice = NULL;
	}

	SF_LOG_I("Platform device creation was done with result: %d", result);
	return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdDestroyPlatformInterface(void)
{
	SF_STATUS result = SF_STATUS_OK;

	platform_driver_unregister(&s_platformDriver);
	platform_device_unregister(s_pPlatformDevice);

	sf_zero_memory(&s_platformDriver, sizeof(struct platform_driver));
	s_pPlatformDevice = NULL;

	SF_LOG_I("Platform device has been removed");

	return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
int SfdSuspendHandler(struct platform_device* pd, pm_message_t state)
{
	SF_LOG_I("%s not implemented", __FUNCTION__);
	return 0;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
int SfdResumeHandler(struct platform_device* pd)
{
	SF_LOG_I("%s not implemented", __FUNCTION__);
	return 0;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
int SfdRemoveHandler(struct platform_device* pd)
{
	SF_LOG_I("%s not implemented", __FUNCTION__);
	return 0;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdShutdownHandler(struct platform_device* pd)
{
	SF_LOG_I("%s not implemented", __FUNCTION__);
}
