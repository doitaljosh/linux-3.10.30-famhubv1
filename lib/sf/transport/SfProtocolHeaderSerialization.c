#include <uapi/linux/sf/core/SfDebug.h>

#include <uapi/linux/sf/transport/SfProtocolHeaderSerialization.h>
#include <uapi/linux/sf/protocol/SfPacket.h>
#include <uapi/linux/sf/transport/SfNetlink.h>

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
SF_STATUS SfSerializeProtocolHeader( const SfProtocolHeader* const pHeader,
                                     SfNetlinkPacket* const pPacket, Int rootAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    struct nlattr* pAttr = NULL;

    if ( !pHeader || !pPacket )
    {
        return SF_STATUS_BAD_ARG;
    }

    pAttr = nla_nest_start( pPacket->pBuffer, rootAttribute );
    if ( pAttr )
    {
        if ( !nla_put_u32( pPacket->pBuffer, SFD_PROTOCOL_HEADER_SIZE_ATTR, pHeader->size ) &&
             !nla_put_u32( pPacket->pBuffer, SFD_PROTOCOL_HEADER_TYPE_ATTR, pHeader->type ) )
        {
            r = SF_STATUS_OK;
        }
        nla_nest_end( pPacket->pBuffer, pAttr );
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfDeserializeProtocolHeader( SfProtocolHeader* const pHeader, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    struct nlattr* hdrAttrs [ SFD_PROTOCOL_HEADER_MAX_ATTR + 1 ] = { 0 };

    if ( !pHeader || !pAttribute )
    {
        return SF_STATUS_BAD_ARG;
    }
    if ( !nla_parse_nested( hdrAttrs, SFD_PROTOCOL_HEADER_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* sizeAttr = hdrAttrs[ SFD_PROTOCOL_HEADER_SIZE_ATTR ];
        struct nlattr* typeAttr = hdrAttrs[ SFD_PROTOCOL_HEADER_TYPE_ATTR ];
        if ( sizeAttr && typeAttr )
        {
            pHeader->size = nla_get_u32( sizeAttr );
            pHeader->type = nla_get_u32( typeAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}
