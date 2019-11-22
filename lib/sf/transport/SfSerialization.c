
#include <uapi/linux/sf/transport/SfSerialization.h>
#include <uapi/linux/sf/transport/SfNetlink.h>
#include <uapi/linux/sf/transport/SfProtocolHeaderSerialization.h>
#include <uapi/linux/sf/transport/SfPacketEnvironmentSerialization.h>
#include <uapi/linux/sf/transport/SfPacketOperationSerialization.h>

#if defined(SF_LEVEL_USER)
    #include "libnl/include/netlink/attr.h"
    #include "libnl/include/netlink/netlink.h"
    #include "libnl/include/netlink/genl/genl.h"
    #include "libnl/include/netlink/genl/ctrl.h"
    #include "libnl/include/netlink/genl/mngt.h"
#else
    #include <net/sock.h>
#endif  // SF_LEVEL_USER

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SfNetlinkPacket* SfSerializePacket( const SfPacket* pPacket )
{
    SfNetlinkPacket* pNPacket = NULL;

    do
    {
#if defined(SF_LEVEL_KERNEL)
        struct nlmsghdr* msgHdr = NULL;
#endif  // SF_LEVEL_KERNEL
        if ( !pPacket )
        {
            SF_LOG_E( "%s(): incoming packet is NULL", __FUNCTION__ );
            break;
        }

        pNPacket = SfCreateNetlinkPacket();
        if ( !pNPacket )
        {
            SF_LOG_E( "%s(): failed to create Netlink packet", __FUNCTION__ );
            break;
        }

#if defined(SF_LEVEL_KERNEL)
        msgHdr = nlmsg_put( pNPacket->pBuffer, 0, 0, SF_PACKET_TYPE_NOTIFICATION, 0, 0 );
#else
        nlmsg_put( pNPacket->pBuffer, 0, 0, SF_PACKET_TYPE_NOTIFICATION, 0, 0 );
#endif  // SF_LEVEL_KERNEL

        // serialize protocol header
        if ( SF_FAILED( SfSerializeProtocolHeader( &pPacket->header, pNPacket,
                                                   SFD_PACKET_HEADER_ATTR ) ) )
        {
            SF_LOG_E( "%s(): failed to serialize protocol header", __FUNCTION__ );
            SfDestroyNetlinkPacket( pNPacket );
            pNPacket = NULL;
            break;
        }

        // serialize packet environment if present
        if ( pPacket->env &&
             SF_FAILED( SfSerializePacketEnvironment( pPacket->env, pNPacket,
                                                      SFD_PACKET_ENVIRONMENT_ATTR ) ) )
        {
            SF_LOG_E( "%s(): failed to serialize packet environment", __FUNCTION__ );
            SfDestroyNetlinkPacket( pNPacket );
            pNPacket = NULL;
            break;
        }

        // serialize packet operation if present
        if ( pPacket->op &&
             SF_FAILED( SfSerializePacketOperation( pPacket->op, pNPacket,
                                                    SFD_PACKET_OPERATION_ATTR ) ) )
        {
            SF_LOG_E( "%s(): failed to serialize packet operation", __FUNCTION__ );
            SfDestroyNetlinkPacket( pNPacket );
            pNPacket = NULL;
            break;
        }

#if defined(SF_LEVEL_KERNEL)
        nlmsg_end( pNPacket->pBuffer, msgHdr );
#endif  // SF_LEVEL_KERNEL
    }
    while ( FALSE );
    return pNPacket;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SfPacket* SfDeserializePacket( const SfNetlinkPacket* pNPacket )
{
    SfPacket* pPacket = NULL;
    do
    {
        struct nlattr* packetAttrs [ SFD_PACKET_MAX_ATTR + 1 ] = { 0 };
        struct nlattr* headerAttr = NULL, *envAttr = NULL, *opAttr = NULL;
        struct nlmsghdr* msgHdr = NULL;

        if ( !pNPacket )
        {
            SF_LOG_E( "%s(): incoming Netlink packet is NULL", __FUNCTION__ );
            break;
        }

        // load SfPacket Netlink attributes from Netlink message
        msgHdr = nlmsg_hdr( pNPacket->pBuffer );
        if ( nlmsg_parse( msgHdr, 0, packetAttrs, SFD_PACKET_MAX_ATTR, NULL ) )
        {
            SF_LOG_E( "%s(): failed to parse Netlink attributes", __FUNCTION__ );
            break;
        }

        // create packet
        pPacket = sf_malloc( sizeof( SfPacket ) );
        if ( !pPacket )
        {
            SF_LOG_E( "%s(): failed to allocate SfPacket structure", __FUNCTION__ );
            break;
        }
        sf_memset( pPacket, 0, sizeof( SfPacket ) );

        headerAttr   = packetAttrs[ SFD_PACKET_HEADER_ATTR ];
        envAttr      = packetAttrs[ SFD_PACKET_ENVIRONMENT_ATTR ];
        opAttr       = packetAttrs[ SFD_PACKET_OPERATION_ATTR ];
        // header attribute must be always present
        if ( !headerAttr )
        {
            SF_LOG_E( "%s(): packet header attribute missing", __FUNCTION__ );
            SfDestroyPacket( pPacket );
            pPacket = NULL;
            break;
        }
        if ( SF_FAILED( SfDeserializeProtocolHeader( &pPacket->header, headerAttr ) ) )
        {
            SF_LOG_E( "%s(): failed to deserialize packet header", __FUNCTION__ );
            SfDestroyPacket( pPacket );
            pPacket = NULL;
            break;
        }

        // deserialize environment if present
        if ( envAttr )
        {
            pPacket->env = SfDeserializePacketEnvironment( envAttr );
            if ( !pPacket->env )
            {
                SF_LOG_E( "%s(): failed to deserialize packet environment", __FUNCTION__ );
                SfDestroyPacket( pPacket );
                pPacket = NULL;
                break;
            }
        }

        // deserialize operation if present
        if ( opAttr )
        {
            pPacket->op = SfDeserializePacketOperation( opAttr );
            if ( !pPacket->op )
            {
                SF_LOG_E( "%s(): failed to deserialize packet operation", __FUNCTION__ );
                SfDestroyPacket( pPacket );
                pPacket = NULL;
                break;
            }
        }
    }
    while ( FALSE );
    return pPacket;
}