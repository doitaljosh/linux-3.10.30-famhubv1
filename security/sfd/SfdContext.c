/**
****************************************************************************************************
* @file SfdContext.c
* @brief Security framework [SF] filter driver [D] main structure that contains all submodules.
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

#include "Sfd.h"

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenContext(SfdContext* const pContext)
{
	SF_STATUS result = SF_STATUS_FAIL;

	result = SfOpenDebuggerContext(NULL);

	/**
	* @note If you want to use this macros in your code, please, check it's implementation before.
	* 	Implementation located in libcore/SfValidator.h
	*/
	result = SF_CONTEXT_SAFE_INITIALIZATION(pContext, SfdContext, SF_CORE_VERSION, SfdCloseContext);

	if (SF_SUCCESS(result))
	{
		do
		{
			/**
			* @note Initialization of the dispatcher context should be done first since it has
			* 	dependency on linux kernel. All other contexts has dependency only on dispatcher
			*	and each other.
			*/
			result = SfdOpenDispatcherContext(&pContext->dispatcher);
			if (SF_FAILED(result))
			{
				SF_LOG_E("%s can not open dispatcher context", __FUNCTION__);
				break;
			}

			// result = SfdOpenNotifierContext(&pContext->notifier);
			// if (SF_FAILED(result))
			// {
			// 	SF_LOG_E("%s Can not open notifier context", __FUNCTION__);
			// 	break;
			// }

			// result = SfdOpenUepContext(&pContext->uep);
			// if (SF_FAILED(result))
			// {
			// 	SF_LOG_E("%s Can not open UEP context", __FUNCTION__);
			// 	break;
			// }

			pContext->header.state = SF_CONTEXT_STATE_INITIALIZED;

		} while(FALSE);

		if (SF_FAILED(result))
		{
			SfdCloseContext(pContext);
		}

		SF_LOG_I( "%s SFD context has been open with result: %d", __FUNCTION__, result );
	}

	return 0;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseContext(SfdContext* const pContext)
{
	SF_STATUS result = SF_STATUS_FAIL;

	if (!SfIsContextValid(&pContext->header, sizeof(SfdContext)))
	{
		SF_LOG_E( "%s Invalid 'pContext'", __FUNCTION__ );
		return SF_STATUS_BAD_ARG;
	}

	result = SfdCloseDispatcherContext(&pContext->dispatcher);
	SF_ASSERT(SF_DEBUG_CLASS_SFD, SF_SUCCESS(result), "Can not close dispatcher context");

	// result = SfdCloseNotifierContext(&pContext->notifier);
	// SF_ASSERT(SF_DEBUG_CLASS_SFD, SF_SUCCESS(result), "Can not close notifier context");

	// result = SfdCloseUepContext(&pContext->uep);
	// SF_ASSERT(SF_DEBUG_CLASS_SFD, SF_SUCCESS(result), "Can not close UEP context");

	pContext->header.size = 0;
	pContext->header.state = SF_CONTEXT_STATE_UNINITIALIZED;
	SF_LOG_I( "%s SFD context has been closed", __FUNCTION__ );
	SfCloseDebuggerContext(NULL);
	return result;
}
