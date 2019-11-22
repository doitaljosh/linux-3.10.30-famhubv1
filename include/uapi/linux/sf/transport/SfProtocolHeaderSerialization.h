#ifndef _SF_PROTOCOL_HEADER_SERIALIZATION_H_
#define _SF_PROTOCOL_HEADER_SERIALIZATION_H_

#include <uapi/linux/sf/transport/netlink/SfNetlinkSerialization.h>
#include <uapi/linux/sf/protocol/SfProtocolHeader.h>

struct nlattr;

/**
****************************************************************************************************
* @brief                            Put protocol header to Netlink packet
* @param [in] pHeader               Packet header
* @param [in,out] pPacket           Pointer to the netlink packet
* @param [in] rootAttribute         Key to store data in the Netlink transport packet
* @return                           SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfSerializeProtocolHeader( const SfProtocolHeader* const pHeader,
                                     SfNetlinkPacket* const pPacket, Int rootAttribute );

/**
****************************************************************************************************
* @brief                            Get protocol header from Netlink packet
* @param [in,out] pHeader           Pointer to the header to be filled
* @param [in] pAttribute            Pointer to the protocol header attribute
* @return                           SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfDeserializeProtocolHeader( SfProtocolHeader* const pHeader, struct nlattr* pAttribute );

#endif /* !_SF_PROTOCOL_HEADER_SERIALIZATION_H_ */
