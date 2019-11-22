/**
****************************************************************************************************
* @file SfPacket.h
* @brief Security framework [SF] filter driver [D] interoperable packet implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 18, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_PACKET_H_
#define _SF_PACKET_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#include "SfOperationsFormat.h"
#include "SfEnvironmentFormat.h"

/**
****************************************************************************************************
* @def SF_PROTOCOL_NUMBER
* @brief Protocol number
****************************************************************************************************
*/
#define SF_PROTOCOL_NUMBER  31

/**
****************************************************************************************************
* @def SF_VALIDATE_PACKET
* @brief Performs validation of the packet structure
* @note Function-like macros. Return value (stack memory).
* @param[in] pPacketInterface An interface to the corresponding packet
* @return SF_STATUS_OK on validation success, SF_STATUS_BAD_ARG on validation fail
****************************************************************************************************
*/
#define SF_VALIDATE_PACKET(pPacketInterface) \
({ \
	SF_STATUS validationResult = SF_STATUS_OK; \
	if (NULL == pPacketInterface && pPacketInterface->size < sizeof(SfProtocolHeader)) \
	{ \
		validationResult = SF_STATUS_BAD_ARG; \
	} \
	validationResult; \
})

/**
****************************************************************************************************
* @typedef SF_PACKET_TYPE
* @brief Packet types definition
* @warning This enumeration currently unused
****************************************************************************************************
*/
typedef enum
{
	SF_PACKET_TYPE_NOTIFICATION = 0x20, ///< Notification packet type
	SF_PACKET_TYPE_EXCEPTION = 0x21, ///< Exception packet type
	SF_PACKET_TYPE_OPERATION = 0x22, ///< Operation packet type
	SF_PACKET_TYPE_MAX = SF_PACKET_TYPE_OPERATION
} SF_PACKET_TYPE;

/**
****************************************************************************************************
* @struct SfPacket
* @brief Packet structure definition
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	SfProtocolHeader header; ///< Packet header
	SfProtocolHeader* env; ///< Environment for packet processing
	SfProtocolHeader* op; ///< Operation to be processed by packet handler
} SfPacket;

/**
****************************************************************************************************
* @typedef SfPacketHandler
* @brief Universal packet handler in scope of Security Filter project
* @param[in] argsInterface Pointer to SfdPacketHeader that is part of system call argument
*	structures. As an argument should be passed address of one of the system call argument
*	structure headers. Further this address should be interpretted by a type of header to
*	corresponding structure type.
* @warning Read documentation about this function type
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise, SF_STATUS_NOT_IMPLEMENTED in case if
*	handler of coresponding packet type is not implemented or not supported by this module
****************************************************************************************************
*/
#if defined(SF_LEVEL_KERNEL)
typedef SFCALL SF_STATUS (*SfPacketHandler)(const SfProtocolHeader* const argsInterface);
#endif /* SF_LEVEL_KERNEL */

/**
****************************************************************************************************
* @brief                    Destroy packet that was allocated on heap(with environment)
* @param [in] pPacket       Packet to be destroyed
* @return
****************************************************************************************************
*/
void SfDestroyPacket( SfPacket* pPacket );

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif	/*	!_SF_PACKET_H_ */
