#ifndef _SF_PACKET_OPERATION_SERIALIZATION_H_
#define _SF_PACKET_OPERATION_SERIALIZATION_H_

#include <uapi/linux/sf/transport/netlink/SfNetlinkSerialization.h>
#include <uapi/linux/sf/protocol/SfProtocolHeader.h>

struct nlattr;

/**
****************************************************************************************************
* @brief                        Serialize packet operation data
* @param [in] pHeader           Header of packet operation
* @param [in] pNetlinkPacket    Netlink packet to hold serialized data
* @param [in] attribute         Netlink attribute type
* @return                       SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfSerializePacketOperation( const SfProtocolHeader* pHeader,
                                      SfNetlinkPacket* pNetlinkPacket, Int attribute );

/**
****************************************************************************************************
* @brief                        Deserialize SF packet operation from Netlink packet
* @param [in] pAttribute        Netlink attribute
* @return                       Protocol header of operation on success, NULL otherwise
****************************************************************************************************
*/
SfProtocolHeader* SfDeserializePacketOperation( struct nlattr* pAttribute );

#endif	/* !_SF_PACKET_OPERATION_SERIALIZATION_H_ */