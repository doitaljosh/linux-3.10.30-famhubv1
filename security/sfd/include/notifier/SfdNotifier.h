/**
****************************************************************************************************
* @file SfdNotifier.h
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

#ifndef	_SFD_NOTIFIER_H_
#define _SFD_NOTIFIER_H_

#include "dispatcher/SfdModuleInterface.h"

/**
****************************************************************************************************
* @brief This structure implement Notifier context
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfContextHeader		header; ///< Container context header
	SfdModuleInterface	module; ///< Container module interface to interact with SfdDispatcher
} SfdNotifierContext;

/**
****************************************************************************************************
* @brief Open notifier context
* @param [in,out] pNotifier Pointer to the notifier context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenNotifierContext(SfdNotifierContext* const pNotifier);

/**
****************************************************************************************************
* @brief Open notifier context
* @param [in,out] pNotifier Pointer to the notifier context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseNotifierContext(SfdNotifierContext* const pNotifier);

#endif	/* !_SFD_NOTIFIER_H_ */
