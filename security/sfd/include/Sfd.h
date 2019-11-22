/**
****************************************************************************************************
* @file Sfd.h
* @brief Security framework [SF] filter driver [D] main structure that contains all submodules.
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_H_
#define _SFD_H_

#include "dispatcher/SfdDispatcher.h"
#include "notifier/SfdNotifier.h"
#include "uep/SfdUep.h"

/**
****************************************************************************************************
* @brief Context of the Security Filter Driver
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfContextHeader 		header; ///< Context header
	SfdDispatcherContext 	dispatcher; ///< Dispatcher module context
	SfdNotifierContext 		notifier; ///< Notifier module context
	SfdUepContext			uep; ///< UEP module context
} SfdContext;

/**
****************************************************************************************************
* @brief Start and initialize Secuiryt Filter Driver with all available submodules.
* @param [in,out] pContext Pointer to Security Filter Driver context structure
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenContext(SfdContext* const pContext);

/**
****************************************************************************************************
* @brief Stop Security Filter Driver and free all resources that was used.
* @param [in,out] pContext Pointer to Security Filter Driver context structure
*
* @return void
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseContext(SfdContext* const pContext);

#endif	/* _SFD_H_ */
