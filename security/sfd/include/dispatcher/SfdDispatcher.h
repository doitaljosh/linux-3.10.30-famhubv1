/**
****************************************************************************************************
* @file SfdDispatcher.h
* @brief Security framework [SF] filter driver [D] modules dispather implementation
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

#ifndef _SFD_DISPATCHER_H_
#define _SFD_DISPATCHER_H_

#include "SfdModuleInterface.h"

/**
****************************************************************************************************
* @brief This structure implements dispatcher context
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfContextHeader 				header; ///< Dispatcher context header
	SfdModuleInterface 				module; ///< Module interface
} SfdDispatcherContext;

/**
***************************************************************************************************
* @brief Open dispather context
* @param [in,out] pDispatcher Pointer to the dispathcer context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
***************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenDispatcherContext(SfdDispatcherContext* const pDispatcher);

/**
***************************************************************************************************
* @brief Close dispatcher context
* @param [in,out] pDispatcher Pointer to the dispathcer context
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
***************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseDispatcherContext(SfdDispatcherContext* const pDispatcher);

/**
****************************************************************************************************
* @brief Clear all modules with registered stubs and send to them notification about it
* @param [in,out] pDispatcher Pointer to modules list
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdClearModulesList(SfdModuleInterface* const pModules);

/**
****************************************************************************************************
* @brief Call the hookchain of the module
* @param [in] pOperation Pointer to the SfOperationHeader structure
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdProcessOperationThroughModules(SfProtocolHeader* const pOperation);

#endif	/* !_SFD_DISPATCHER_H_ */
