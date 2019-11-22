#include <uapi/linux/sf/transport/SfPacketEnvironmentSerialization.h>
#include <uapi/linux/sf/transport/SfProtocolHeaderSerialization.h>
#include <uapi/linux/sf/transport/SfNetlink.h>

#include <uapi/linux/sf/protocol/SfPacket.h>

#if defined(SF_LEVEL_USER)
    #include <netlink/attr.h>
#else
    #include <net/sock.h>
#endif  // SF_LEVEL_USER

typedef SF_STATUS (* PutCallback)( const SfProtocolHeader*, SfNetlinkPacket*, Int );

#if defined(SF_LEVEL_KERNEL)
static Char* nla_strdup( struct nlattr* pAttr )
{
    Char* data = (Char*)( nla_data( pAttr ) );
    Uint length = strlen( data ) + 1;
    Char* out = sf_malloc( length );
    if ( out )
    {
        memcpy( out, data, length );
    }
    return out;
}
#endif  // SF_LEVEL_KERNEL

#if defined(SF_LEVEL_USER)
static int nla_put_s32( struct nl_msg *msg, int attrtype, int32_t value )
{
    return nla_put( msg, attrtype, sizeof(int32_t), &value );
}

static int32_t nla_get_s32( struct nlattr* nla )
{
    return *(int32_t*)nla_data( nla );
}
#endif  // SF_LEVEL_USER

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfSerializeExecutionEnvironment( const SfExecutionEnvironmentInfo* const pEnv,
                                           SfNetlinkPacket* const pPacket, Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    struct nlattr* pAttr = NULL;

    if ( !pEnv || !pPacket )
    {
        return SF_STATUS_BAD_ARG;
    }

    pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( !nla_put_string( pPacket->pBuffer, SFD_EXEC_ENV_PROCESS_NAME_ATTR,
                              pEnv->pProcessName )                                           &&
             !nla_put_u32( pPacket->pBuffer, SFD_EXEC_ENV_PROCESS_ID_ATTR, pEnv->processId ) &&
             !nla_put_s32( pPacket->pBuffer, SFD_EXEC_ENV_RESULT_ATTR, pEnv->sysCallResult ) &&
             !nla_put_u64( pPacket->pBuffer, SFD_EXEC_ENV_TIME_ATTR, pEnv->timeStamp ) )
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
SF_STATUS SfDeserializeExecutionEnvironment( SfExecutionEnvironmentInfo* pEnv,
                                             struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    struct nlattr* envAttrs [ SFD_EXEC_ENV_MAX_ATTR + 1 ] = { 0 };

    if ( !pEnv || !pAttribute )
    {
        return SF_STATUS_BAD_ARG;
    }
    if ( !nla_parse_nested( envAttrs, SFD_EXEC_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        pEnv->processId = nla_get_u32( envAttrs[ SFD_EXEC_ENV_PROCESS_ID_ATTR ] );
        pEnv->sysCallResult = nla_get_s32( envAttrs[ SFD_EXEC_ENV_RESULT_ATTR ] );
        pEnv->timeStamp = nla_get_u64( envAttrs[ SFD_EXEC_ENV_TIME_ATTR ] );
        pEnv->pProcessName = nla_strdup( envAttrs[ SFD_EXEC_ENV_PROCESS_NAME_ATTR ] );
        r = SF_STATUS_OK;
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutFileEnvironment( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                     Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfFileEnvironment* pEnv = (const SfFileEnvironment*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS( SfSerializeExecutionEnvironment( &pEnv->processContext, pPacket,
                                                          SFD_FILE_ENV_EXEC_ENV_ATTR ) )       &&
             !nla_put_string( pPacket->pBuffer, SFD_FILE_ENV_FILE_NAME_ATTR, pEnv->pFileName ) &&
             !nla_put_u64( pPacket->pBuffer, SFD_FILE_ENV_INODE_ATTR, pEnv->inode ) )
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
static SF_STATUS LoadFileEnvironment( SfFileEnvironment* pEnv, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* envAttrs [ SFD_FILE_ENV_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( envAttrs, SFD_FILE_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* execEnvAttr  = envAttrs[ SFD_FILE_ENV_EXEC_ENV_ATTR ];
        struct nlattr* fileNameAttr = envAttrs[ SFD_FILE_ENV_FILE_NAME_ATTR ];
        struct nlattr* inodeAttr    = envAttrs[ SFD_FILE_ENV_INODE_ATTR ];
        if ( execEnvAttr && fileNameAttr && inodeAttr )
        {
            SfDeserializeExecutionEnvironment( &pEnv->processContext, execEnvAttr );
            pEnv->inode = nla_get_u64( inodeAttr );
            pEnv->pFileName = nla_strdup( fileNameAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutProcessEnvironment( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                        Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfProcessEnvironment* pEnv = (const SfProcessEnvironment*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS( SfSerializeExecutionEnvironment( &pEnv->processContext, pPacket,
                                                          SFD_PROC_ENV_EXEC_ENV_ATTR ) ) &&
             !nla_put_string( pPacket->pBuffer, SFD_PROC_ENV_PROCESS_NAME_ATTR,
                              pEnv->pProcessName )                                       &&
             !nla_put_u64( pPacket->pBuffer, SFD_PROC_ENV_PROCESS_INODE_ATTR,
                           pEnv->processImageId ) )
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
static SF_STATUS LoadProcessEnvironment( SfProcessEnvironment* pEnv, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* envAttrs [ SFD_PROC_ENV_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( envAttrs, SFD_PROC_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* execEnvAttr = envAttrs[ SFD_PROC_ENV_EXEC_ENV_ATTR ];
        struct nlattr* procNameAttr = envAttrs[ SFD_PROC_ENV_PROCESS_NAME_ATTR ];
        struct nlattr* imgIdAttr = envAttrs[ SFD_PROC_ENV_PROCESS_INODE_ATTR ];
        if ( execEnvAttr && procNameAttr && imgIdAttr )
        {
            SfDeserializeExecutionEnvironment( &pEnv->processContext, execEnvAttr );
            pEnv->processImageId = nla_get_u64( imgIdAttr );
            pEnv->pProcessName = nla_strdup( procNameAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutMmapEnvironment( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                     Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfMmapEnvironment* pEnv = (const SfMmapEnvironment*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS( SfSerializeExecutionEnvironment( &pEnv->processContext, pPacket,
                                                          SFD_MMAP_ENV_EXEC_ENV_ATTR ) ) &&
             !nla_put_string( pPacket->pBuffer, SFD_MMAP_ENV_LIBRARY_NAME_ATTR,
                              pEnv->pLibraryName )                                       &&
             !nla_put_u64( pPacket->pBuffer, SFD_MMAP_ENV_INODE_ATTR, pEnv->inode ) )
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
static SF_STATUS LoadMmapEnvironment( SfMmapEnvironment* pEnv, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* envAttrs [ SFD_MMAP_ENV_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( envAttrs, SFD_MMAP_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* execEnvAttr = envAttrs[ SFD_MMAP_ENV_EXEC_ENV_ATTR ];
        struct nlattr* libNameAttr = envAttrs[ SFD_MMAP_ENV_LIBRARY_NAME_ATTR ];
        struct nlattr* inodeAttr   = envAttrs[ SFD_MMAP_ENV_INODE_ATTR ];
        if ( execEnvAttr && libNameAttr && inodeAttr )
        {
            SfDeserializeExecutionEnvironment( &pEnv->processContext, execEnvAttr );
            pEnv->inode = nla_get_u64( inodeAttr );
            pEnv->pLibraryName = nla_strdup( libNameAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS PutNetworkEnvironment( const SfProtocolHeader* pHeader, SfNetlinkPacket* pPacket,
                                        Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;
    const SfNetworkEnvironment* pEnv = (const SfNetworkEnvironment*)pHeader;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS( SfSerializeExecutionEnvironment( &pEnv->processContext, pPacket,
                                                          SFD_NET_ENV_EXEC_ENV_ATTR ) ) &&
             !nla_put_u32( pPacket->pBuffer, SFD_NET_ENV_ADDR_ATTR, pEnv->addr )        &&
             !nla_put_u16( pPacket->pBuffer, SFD_NET_ENV_PORT_ATTR, pEnv->port ) )
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
static SF_STATUS LoadNetworkEnvironment( SfNetworkEnvironment* pEnv, struct nlattr* pAttribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* envAttrs [ SFD_NET_ENV_MAX_ATTR + 1 ] = { 0 };
    if ( !nla_parse_nested( envAttrs, SFD_NET_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* execEnvAttr  = envAttrs[ SFD_NET_ENV_EXEC_ENV_ATTR ];
        struct nlattr* addrAttr     = envAttrs[ SFD_NET_ENV_ADDR_ATTR ];
        struct nlattr* portAttr     = envAttrs[ SFD_NET_ENV_PORT_ATTR ];
        if ( execEnvAttr && addrAttr && portAttr )
        {
            SfDeserializeExecutionEnvironment( &pEnv->processContext, execEnvAttr );
            pEnv->addr = nla_get_u32( addrAttr );
            pEnv->port = nla_get_u16( portAttr );
            r = SF_STATUS_OK;
        }
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SerializeEnvironment( const SfProtocolHeader* pHeader, PutCallback cb,
                                       SfNetlinkPacket* pPacket, Int attribute )
{
    SF_STATUS r = SF_STATUS_FAIL;

    struct nlattr* pAttr = nla_nest_start( pPacket->pBuffer, attribute );
    if ( pAttr )
    {
        if ( SF_SUCCESS( SfSerializeProtocolHeader( pHeader, pPacket, SFD_ENV_HEADER_ATTR ) ) &&
             SF_SUCCESS( cb( pHeader, pPacket, SFD_ENV_DATA_ATTR ) ) )
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
SF_STATUS SfSerializePacketEnvironment( const SfProtocolHeader* const pHeader,
                                        SfNetlinkPacket* const pNetlinkPacket, Int attribute )
{
    PutCallback cb = NULL;
    if ( !pHeader || !pNetlinkPacket )
    {
        return SF_STATUS_BAD_ARG;
    }

    switch ( pHeader->type )
    {
        case SF_ENVIRONMENT_TYPE_FILE:
            cb = PutFileEnvironment;
            break;

        case SF_ENVIRONMENT_TYPE_PROCESS:
            cb = PutProcessEnvironment;
            break;

        case SF_ENVIRONMENT_TYPE_NETWORK:
            cb = PutNetworkEnvironment;
            break;

        case SF_ENVIRONMENT_TYPE_MMAP:
            cb = PutMmapEnvironment;
            break;

        default:
            SF_LOG_E( "%s(): unexpected environment type: %d", __FUNCTION__, pHeader->type );
            break;
    }
    return ( cb ) ? SerializeEnvironment( pHeader, cb, pNetlinkPacket, attribute ) : SF_STATUS_FAIL;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SfProtocolHeader* DeserializeEnvironment( const SfProtocolHeader* envHeader,
                                                 struct nlattr* envDataAttr )
{
    SfProtocolHeader* pPrtclHdr = NULL;

    switch ( envHeader->type )
    {
        case SF_ENVIRONMENT_TYPE_FILE:
        {
            SfFileEnvironment* pFileEnv = NULL;
            pPrtclHdr = SF_CREATE_ENVIRONMENT( SfFileEnvironment, SF_ENVIRONMENT_TYPE_FILE );
            pFileEnv = (SfFileEnvironment*)pPrtclHdr;
            pFileEnv->header = *envHeader;
            LoadFileEnvironment( pFileEnv, envDataAttr );
            break;
        }

        case SF_ENVIRONMENT_TYPE_PROCESS:
        {
            SfProcessEnvironment* pProcEnv = NULL;
            pPrtclHdr = SF_CREATE_ENVIRONMENT( SfProcessEnvironment, SF_ENVIRONMENT_TYPE_PROCESS );
            pProcEnv = (SfProcessEnvironment*)pPrtclHdr;
            pProcEnv->header = *envHeader;
            LoadProcessEnvironment( pProcEnv, envDataAttr );
            break;
        }

        case SF_ENVIRONMENT_TYPE_NETWORK:
        {
            SfNetworkEnvironment* pNetEnv = NULL;
            pPrtclHdr = SF_CREATE_ENVIRONMENT( SfNetworkEnvironment, SF_ENVIRONMENT_TYPE_NETWORK );
            pNetEnv = (SfNetworkEnvironment*)pPrtclHdr;
            pNetEnv->header = *envHeader;
            LoadNetworkEnvironment( pNetEnv, envDataAttr );
            break;
        }

        case SF_ENVIRONMENT_TYPE_MMAP:
        {
            SfMmapEnvironment* pMmapEnv = NULL;
            pPrtclHdr = SF_CREATE_ENVIRONMENT( SfMmapEnvironment, SF_ENVIRONMENT_TYPE_MMAP );
            pMmapEnv = (SfMmapEnvironment*)pPrtclHdr;
            pMmapEnv->header = *envHeader;
            LoadMmapEnvironment( pMmapEnv, envDataAttr );
            break;
        }

        default:
        {
            SF_LOG_E( "%s(): unexpected environment type: %d", __FUNCTION__, envHeader->type );
            break;
        }
    }
    return pPrtclHdr;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SfProtocolHeader* SfDeserializePacketEnvironment( struct nlattr* pAttribute )
{
    SfProtocolHeader* pPrtclHdr = NULL;
    struct nlattr* envAttrs [ SFD_ENV_MAX_ATTR + 1 ] = { 0 };

    if ( !pAttribute )
    {
        return NULL;
    }
    if ( !nla_parse_nested( envAttrs, SFD_ENV_MAX_ATTR, pAttribute, NULL ) )
    {
        struct nlattr* headerAttr   = envAttrs[ SFD_ENV_HEADER_ATTR ];
        struct nlattr* envDataAttr  = envAttrs[ SFD_ENV_DATA_ATTR ];
        if ( headerAttr && envDataAttr )
        {
            SfProtocolHeader envHeader;
            SfDeserializeProtocolHeader( &envHeader, headerAttr );
            pPrtclHdr = DeserializeEnvironment( &envHeader, envDataAttr );
        }
    }
    return pPrtclHdr;
}