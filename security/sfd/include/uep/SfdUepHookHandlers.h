/**
****************************************************************************************************
* @file SfdUepHookHandlers.h
* @brief Security framework [SF] filter driver [D] hook handlers for system calls that processed by
*	UEP submodule.
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 10, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_UEP_HOOK_HANDLERS_H_
#define _SFD_UEP_HOOK_HANDLERS_H_

#include "dispatcher/SfdModuleInterface.h"

/**
****************************************************************************************************
* @brief Packet handler of the Uep module
* @param[in] pPacketInterface Packet header with packet type. This packet should be casted to
* 	appropriate packet type
* @see SfdSysCallHandler
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise, SF_STATUS_NOT_IMPLEMENTED in case if
*	handler of coresponding packet type is not implemented or not supported by this module
****************************************************************************************************
*/
SF_STATUS SfdUepPacketHandler(const SfProtocolHeader* const pPacketInterface);

/**
****************************************************************************************************
* @brief Verifies digital signature of the dynamic library
* @param[in] pOperationInterface Pointer to the operation interface
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyDynamicLibrary(const SfProtocolHeader* const pOperationInterface);

/**
****************************************************************************************************
* @brief Verifies digital signature of the executable binary
* @param[in] pOperationInterface Pointer to the operation interface
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyExecutableBinary(const SfProtocolHeader* const pOperationInterface);

/**
****************************************************************************************************
* @brief Verifies digital signature of the kernel module
* @param[in] pOperationInterface Pointer to the operation interface
*
* @return SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyKernelModule(const SfProtocolHeader* const pOperationInterface);

/**
****************************************************************************************************
* @brief Verifies digital signature of the file trying to be mapped
* @param[in] pFile Pointer to the file descriptor structure
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyFileSignature(struct file* const pFile);

SF_STATUS SetupDuidHash( const Char* duid );

#endif	/* !_SFD_UEP_HOOK_HANDLERS_ */
