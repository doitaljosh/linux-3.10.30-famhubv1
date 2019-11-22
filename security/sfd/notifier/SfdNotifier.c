/**
****************************************************************************************************
* @file SfdNotifier.c
* @brief Security framework [SF] filter driver [D] notifier context implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdNotifier.h"
#include "SfdNotifierHookHandlers.h"

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenNotifierContext(SfdNotifierContext* const pNotifier)
{
	SF_STATUS result = SF_STATUS_FAIL;

	/**
	* If you want to use this macros in your code, please, check it's implementation before.
	* Implementation located in libcore/SfValidator.h
	*/
	SF_CONTEXT_SAFE_INITIALIZATION(pNotifier, SfdNotifierContext,
		SF_CORE_VERSION, SfdCloseNotifierContext);

	pNotifier->module.moduleType = SFD_MODULE_TYPE_NOTIFIER;
	pNotifier->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE] = NULL;
	pNotifier->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION] = SfdNotifierPacketHandler;

	pNotifier->header.state = SF_CONTEXT_STATE_INITIALIZED;
	result = SfdRegisterModule(&pNotifier->module);

	SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
	return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseNotifierContext(SfdNotifierContext* const pNotifier)
{
	SF_STATUS result = SF_STATUS_OK;

	if (!SfIsContextValid(&pNotifier->header, sizeof(SfdNotifierContext)))
	{
		SF_LOG_E("%s takes (pNotifier = %p) argument", __FUNCTION__, pNotifier);
		result = SF_STATUS_BAD_ARG;
	}

	pNotifier->header.state = SF_CONTEXT_STATE_UNINITIALIZED;

	SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
	return result;
}
