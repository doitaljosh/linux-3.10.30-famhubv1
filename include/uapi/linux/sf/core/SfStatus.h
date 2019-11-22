/**
****************************************************************************************************
* @file SfStatus.h
* @brief Security framework [SF] return codes and helpers
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 4, 2014 13:26
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_STATUS_H_
#define _SF_STATUS_H_

#include "SfTypes.h"

/**
****************************************************************************************************
* @brief Security framework status enumeration
****************************************************************************************************
*/
typedef enum
{
    SF_STATUS_OK = 0,                       ///< Operation has been finished successfully
    SF_STATUS_PENDING = 1,                  ///< Everything is ok but resource busy (try later)
    SF_STATUS_UEP_SIGNATURE_CORRECT = 2,    ///< Signature is correct
    SF_STATUS_UEP_SIGNATURE_INCORRECT = 3,  ///< Signature is incorrect
    SF_STATUS_UEP_FILE_NOT_SIGNED = 4,      ///< File is not signed
    SF_STATUS_UEP_FILE_ALREADY_SIGNED = 5,  ///< File is already signed
    SF_STATUS_UEP_FILE_IS_NOT_ELF = 6,      ///< File is not ELF. Must be skipped
    SF_STATUS_RESOURCE_BLOCK = 7,           ///< Block resource at real-time by the rules list
    SF_STATUS_UEP_SIGNATURE_DUID = 8,       ///< File signature is DUID hash
    SF_STATUS_FAIL = -(1),                  ///< Operation has been failed
    SF_STATUS_BAD_ARG = -(2),               ///< Bad arguments was passed
    SF_STATUS_NOT_IMPLEMENTED = -(3),       ///< Function currently not implemented
    SF_STATUS_ALREADY_INITIALIZED = -(4),   ///< The object already initialized
    SF_STATUS_ACCESS_DENIED = -(5),         ///< Access denied
    SF_STATUS_NOT_INITIALIZED = (-9),       ///< When instance is not initialized
    SF_STATUS_DESTINATION_UNREACHABLE = (-10),
    SF_STATUS_NOT_ENOUGH_RESOURCE = (-11),      ///< Even though resource is not enough, sfd must not block it
    SF_STATUS_MAX = (Int) -1 ///< Max value
} SF_STATUS;

/**
****************************************************************************************************
* @brief Security framework status SUCCESS checker
****************************************************************************************************
*/
#define SF_SUCCESS(x) (x >= 0)

/**
****************************************************************************************************
* @brief Security framework status FAILED checker
****************************************************************************************************
*/
#define SF_FAILED(x) (x < 0)

#endif /* !_SF_STATUS_H_ */
