#include <uapi/linux/sf/transport/SfPacketOperationSerialization.h>
#include <uapi/linux/sf/transport/SfProtocolHeaderSerialization.h>
#include <uapi/linux/sf/transport/SfNetlink.h>

#include <uapi/linux/sf/protocol/SfOperationsFormat.h>
#include <uapi/linux/sf/protocol/SfPacket.h>

#if defined(SF_LEVEL_USER)
    #include <netlink/attr.h>
#else
    #include <net/sock.h>
#endif  // SF_LEVEL_USER

#if defined(SF_LEVEL_KERNEL)
/**
****************************************************************************************************
*
****************************************************************************************************
*/
static Char* nla_strdup( struct nlattr* pAttr )
{
    Char* out = NULL, *data = NULL;
    data = (Char*)( nla_data( pAttr ) );
    if ( data )
    {
        Uint length = strlen( data ) + 1;
        out = sf_malloc( length );
        if ( out )
        {
            memcpy( out, data, length );
        }
    }
    return out;
}
#endif  // SF_LEVEL_KERNEL

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutOperationRule( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                   Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfOperationBlockRule* pOp = (const SfOperationBlockRule*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( !nla_put_u32( pPacket->pBuffer, SFD_OP_RULE_TYPE_ATTR, pOp->ruleType ) &&
             !nla_put_u32( pPacket->pBuffer, SFD_OP_RULE_ACTION_ATTR, pOp->action ) &&
             !nla_put_u32( pPacket->pBuffer, SFD_OP_RULE_ADDR_ATTR, pOp->ipAddr )   &&
             !nla_put_u64( pPacket->pBuffer, SFD_OP_RULE_INODE_ATTR, pOp->fileInode ) )
        {
            r = SF_STATUS_OK;
        }
        nla_nest_end( pPacket->pBuffer, pAttr );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS LoadOperationRule( SfOperationBlockRule* pOp, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* opAttrs [ SFD_OP_RULE_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( opAttrs, SFD_OP_RULE_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* typeAttr   = opAttrs[ SFD_OP_RULE_TYPE_ATTR ];
        struct nlattr* actionAttr = opAttrs[ SFD_OP_RULE_ACTION_ATTR ];
        struct nlattr* addrAttr   = opAttrs[ SFD_OP_RULE_ADDR_ATTR ];
        struct nlattr* inodeAttr  = opAttrs[ SFD_OP_RULE_INODE_ATTR ];

        if ( typeAttr && actionAttr && addrAttr && inodeAttr )
        {
            pOp->ruleType  = nla_get_u32( typeAttr );
            pOp->action    = nla_get_u32( actionAttr );
            pOp->ipAddr    = nla_get_u32( addrAttr );
            pOp->fileInode = nla_get_u64( inodeAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutOperationSetupDUID( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                        Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfOperationSetupDUID* pOp = (const SfOperationSetupDUID*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( !nla_put_string( pPacket->pBuffer, SFD_OP_DUID_DUID_ATTR, pOp->pDUID ) )
        {
            r = SF_STATUS_OK;
        }
        nla_nest_end( pPacket->pBuffer, pAttr );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS LoadOperationSetupDUID( SfOperationSetupDUID* pOp, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* opAttrs [ SFD_OP_DUID_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( opAttrs, SFD_OP_DUID_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* duidAttr = opAttrs[ SFD_OP_DUID_DUID_ATTR ];
        if ( duidAttr )
        {
            pOp->pDUID = nla_strdup( duidAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SerializeOperationRule( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                         Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS(SfSerializeProtocolHeader( pHeader, pPacket, SFD_OP_HEADER_ATTR )) &&
             SF_SUCCESS(PutOperationRule( pHeader, pPacket, SFD_OP_DATA_ATTR )) )
        {
            r = SF_STATUS_OK;
        }
        nla_nest_end( pPacket->pBuffer, pAttr );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SerializeOperationSetupDUID( const SfProtocolHeader* pHeader,
                                              SfNetlinkPacket* pPacket, Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS(SfSerializeProtocolHeader( pHeader, pPacket, SFD_OP_HEADER_ATTR )) &&
             SF_SUCCESS(PutOperationSetupDUID( pHeader, pPacket, SFD_OP_DATA_ATTR )) )
        {
            r = SF_STATUS_OK;
        }
        nla_nest_end( pPacket->pBuffer, pAttr );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfSerializePacketOperation( const SfProtocolHeader* pHeader,
                                      SfNetlinkPacket* pNetlinkPacket, Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    if ( !pHeader || !pNetlinkPacket )
    {
        return SF_STATUS_BAD_ARG;
    }

    switch ( pHeader->type )
    {
        case SF_OPERATION_TYPE_RULE:
            r = SerializeOperationRule( pHeader, pNetlinkPacket, attribute );
            break;

        case SF_OPERATION_TYPE_SETUP_DUID:
            r = SerializeOperationSetupDUID( pHeader, pNetlinkPacket, attribute );
            break;

        default:
            // this is hack to send packets with event notifications to userspace
            // because packet in kernel contains operation with LSM arguments that
            // can not be serialized
            r = SF_STATUS_OK;
            break;
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SfProtocolHeader* DeserializeOperation( const SfProtocolHeader* opHeader,
                                               struct nlattr* opDataAttr )
{
    SfProtocolHeader* pHeader = NULL;

    switch ( opHeader->type )
    {
        case SF_OPERATION_TYPE_RULE:
        {
            SfOperationBlockRule* pRuleOp = NULL;
            pHeader = SF_CREATE_OPERATION( SfOperationBlockRule, SF_OPERATION_TYPE_RULE );
            pRuleOp = (SfOperationBlockRule*)pHeader;
            pRuleOp->header = *opHeader;
            LoadOperationRule( pRuleOp, opDataAttr );
            break;
        }

        case SF_OPERATION_TYPE_SETUP_DUID:
        {
            SfOperationSetupDUID* pDuidOp = NULL;
            pHeader = SF_CREATE_OPERATION( SfOperationSetupDUID, SF_OPERATION_TYPE_SETUP_DUID );
            pDuidOp = (SfOperationSetupDUID*)pHeader;
            pDuidOp->header = *opHeader;
            LoadOperationSetupDUID( pDuidOp, opDataAttr );
            break;
        }

        default:
            SF_LOG_E( "%s(): unsupported operation type %d", __FUNCTION__, opHeader->type );
            break;
    }
    return pHeader;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SfProtocolHeader* SfDeserializePacketOperation( struct nlattr* pAttribute )
{
    SfProtocolHeader* pHeader = NULL;
    struct nlattr* opAttrs [ SFD_OP_MAX_ATTR + 1 ] = { 0 };

    if ( !pAttribute )
    {
        return NULL;
    }
    if ( !nla_parse_nested( opAttrs, SFD_OP_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* headerAttr = opAttrs[ SFD_OP_HEADER_ATTR ];
        struct nlattr* dataAttr   = opAttrs[ SFD_OP_DATA_ATTR ];
        if ( headerAttr && dataAttr )
        {
            SfProtocolHeader opHeader;
            SfDeserializeProtocolHeader( &opHeader, headerAttr );
            pHeader = DeserializeOperation( &opHeader, dataAttr );
        }
    }
    return pHeader;
}