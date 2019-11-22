#ifndef _SF_PACKET_ENVIRONMENT_SERIALIZATION_H_
#define _SF_PACKET_ENVIRONMENT_SERIALIZATION_H_

#include <uapi/linux/sf/transport/netlink/SfNetlinkSerialization.h>
#include <uapi/linux/sf/protocol/SfProtocolHeader.h>
#include <uapi/linux/sf/protocol/SfEnvironmentFormat.h>

struct nlattr;

/**
****************************************************************************************************
* @brief                        Serialize execution environment to Netlink packet
* @param [in] pEnv              Execution environment
* @param [in,out] pPacket       Netlink packet to hold serialized data
* @param [in] attribute         Netlink attribute type
* @return                       SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfSerializeExecutionEnvironment( const SfExecutionEnvironmentInfo* const pEnv,
                                           SfNetlinkPacket* const pPacket, Int attribute );

/**
****************************************************************************************************
* @brief                        Deserialize execution environment from Netlink packet
* @param [in,out] pEnv          Execution environment
* @param [in] pAttribute        Netlink attribute
* @return                       SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfDeserializeExecutionEnvironment( SfExecutionEnvironmentInfo* pEnv,
                                             struct nlattr* pAttribute );

/**
****************************************************************************************************
* @brief                        Serialize SF packet environment
* @param [in] pHeader           Header of packet environment
* @param [in] pNetlinkPacket    Netlink packet to hold serialized data
* @param [in] attribute         Netlink attribute type
* @return                       SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SfSerializePacketEnvironment( const SfProtocolHeader* const pHeader,
                                        SfNetlinkPacket* const pNetlinkPacket, Int attribute );

/**
****************************************************************************************************
* @brief                        Deserialize SF packet environment from Netlink packet
* @param [in] pAttribute        Netlink attribute
* @return                       Protocol header of environment on success, NULL otherwise
****************************************************************************************************
*/
SfProtocolHeader* SfDeserializePacketEnvironment( struct nlattr* pAttribute );

#endif  /* !_SF_PACKET_ENVIRONMENT_SERIALIZATION_H_ */