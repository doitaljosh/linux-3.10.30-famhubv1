/**
****************************************************************************************************
* @file SfValidator.h
* @brief Security framework [SF] common validation routines
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Mar 7, 2014 13:26
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_VALIDATOR_H_
#define _SF_VALIDATOR_H_

#include "SfTypes.h"

#ifdef __cplusplus 
extern "C" {
#endif /* !__cplusplus */

/**
****************************************************************************************************
* @brief Perform checking of the context by validating structure size and pointer state. In case if 
* 		 structure was not initialized yet, it will be filled by valid data. If context structure
* 		 contains valid data, context will be closed and objectErrHandler will be called.
* @warning This is function simulation by using macro
* @param [in,out] objectName Pointer to the object that contains context header structure
* @param [in] objectType Type of the objectName pointer
* @param [in] objectVersion Version of the context object
* @param [in] objectErrHandler Pointer to the function that will be called, if object is invalid or
*	already initialized. objectErrHandler should take as an parameter pointer to the objectType.
* @return SF_STATUS_OK on successfull initialization, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
#define SF_CONTEXT_SAFE_INITIALIZATION(objectName, objectType, objectVersion, objectErrHandler) \
	({ \
		SF_STATUS contextInitailizationResult = SF_STATUS_FAIL; \
		if(!SfIsContextInitialized(&objectName->header, sizeof(objectType))) \
		{ \
			sf_zero_memory(objectName, sizeof(objectType)); \
			objectName->header.size = sizeof(objectType); \
			objectName->header.version = objectVersion; \
			contextInitailizationResult = SF_STATUS_OK; \
		} \
		else \
		{ \
			SF_LOG_E("The #objectName was requested to be initailized \
				twice or it has invalid structure"); \
			objectErrHandler(objectName); \
		} \
		contextInitailizationResult; \
	})

/**
****************************************************************************************************
* @brief Check if the size of the data equal to size of a data type.
* @param [in] pContextHeader pointer to the context header
* @param [in] size full size of structure owner
*
* @return TRUE on success,  FALSE otherwise
****************************************************************************************************
*/
SFLIB Bool SFAPI SfIsContextValid(const SfContextHeader* pContextHeader, Uint32 size);

/**
****************************************************************************************************
* @brief Check if the context is valid and initialized
* @param [in] pContextHeader pointer to the context header
* @param [in] size full size of structure owner
*
* @return TRUE on success,  FALSE otherwise
****************************************************************************************************
*/
SFLIB Bool SFAPI SfIsContextInitialized(const SfContextHeader* pContextHeader, Uint32 size);

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif	/* !_SF_VALIDATOR_H_ */
