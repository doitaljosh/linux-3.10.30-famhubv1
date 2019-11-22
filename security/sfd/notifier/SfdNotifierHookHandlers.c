/**
****************************************************************************************************
* @file SfdNotifierHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handlers for system calls that processed by
*        Notifier submodule.
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

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdNotifierHookHandlers.h"
#include "dispatcher/SfdDispatcher.h"

#include <linux/in.h>
#include <linux/sched.h>
/**
* @brief Main dispatcher context.
*/
extern SfdDispatcherContext* g_pDispatcher;

typedef SfProtocolHeader* (*SetupEnvCallback)( const SfProtocolHeader* pOps );

/**
****************************************************************************************************
* @brief                    Get time from system boot in msecs
* @return                   Time from system boot in msecs
****************************************************************************************************
*/
static inline Uint64 GetTime( void )
{
    return jiffies_to_msecs( jiffies );
}

/**
****************************************************************************************************
* @brief                    Setup open() syscall environment
* @param [in] pOps          Pointer to open() operation environment
* @return                   Pointer to SfFileEnvironment on success, NULL otherwise
****************************************************************************************************
*/
static SfProtocolHeader* SetupOpenEnvironment( const SfProtocolHeader* pOps )
{
    SfProtocolHeader* pEnvHeader = SF_CREATE_ENVIRONMENT( SfFileEnvironment,
                                                          SF_ENVIRONMENT_TYPE_FILE );
    if ( pEnvHeader )
    {
        const SfOperationFileOpen* pArgs = (const SfOperationFileOpen*)pOps;
        SfFileEnvironment* pEnv          = (SfFileEnvironment*)pEnvHeader;
        if ( SF_FAILED(SfdFillExecutionEnvironment( &pEnv->processContext, current, GetTime(),
                                                    pArgs->result )) ||
             SF_FAILED(SfdFillFileEnvironment( pEnv, pArgs )) )
        {
            SfDestroyFileEnvironment( pEnv );
            pEnvHeader = NULL;
        }
    }
    else
        SF_LOG_E( "%s(): failed to create open environment", __FUNCTION__ );
    return pEnvHeader;
}

/**
****************************************************************************************************
* @brief                    Setup mmap() syscall environment
* @param [in] pOps          Pointer to mmap() operation environment
* @return                   Pointer to SfMmapEnvironment on success, NULL otherwise
****************************************************************************************************
*/
static SfProtocolHeader* SetupMmapEnvironment( const SfProtocolHeader* pOps )
{
    SfProtocolHeader* pEnvHeader = SF_CREATE_ENVIRONMENT( SfMmapEnvironment,
                                                          SF_ENVIRONMENT_TYPE_MMAP );
    if ( pEnvHeader )
    {
        const SfOperationFileMmap* pArgs = (const SfOperationFileMmap*)pOps;
        SfMmapEnvironment* pEnv          = (SfMmapEnvironment*)pEnvHeader;
        if ( SF_FAILED(SfdFillExecutionEnvironment( &pEnv->processContext, current, GetTime(),
                                                    pArgs->result )) ||
             SF_FAILED(SfdFillMmapEnvironment( pEnv, pArgs )) )
        {
            SfDestroyMmapEnvironment( pEnv );
            pEnvHeader = NULL;
        }
    }
    else
        SF_LOG_E( "%s(): failed to create mmap environment", __FUNCTION__ );
    return pEnvHeader;
}

/**
****************************************************************************************************
* @brief                    Setup execve() syscall environment
* @param [in] pOps          Pointer to execve() operation environment
* @return                   Pointer to SfProcessEnvironment on success, NULL otherwise
****************************************************************************************************
*/
static SfProtocolHeader* SetupExecveEnvironment( const SfProtocolHeader* pOps )
{
    SfProtocolHeader* pEnvHeader = SF_CREATE_ENVIRONMENT( SfProcessEnvironment,
                                                          SF_ENVIRONMENT_TYPE_PROCESS );
    if ( pEnvHeader )
    {
        const SfOperationBprmCheckSecurity* pArgs = (const SfOperationBprmCheckSecurity*)pOps;
        SfProcessEnvironment* pEnv                = (SfProcessEnvironment*)pEnvHeader;
        if ( SF_FAILED(SfdFillExecutionEnvironment( &pEnv->processContext, current, GetTime(),
                                                    pArgs->result )) ||
             SF_FAILED(SfdFillProcessEnvironment( pEnv, pArgs )) )
        {
            SfDestroyProcessEnvironment( pEnv );
            pEnvHeader = NULL;
        }
    }
    else
        SF_LOG_E( "%s(): failed to create execve environment", __FUNCTION__ );
    return pEnvHeader;
}

/**
****************************************************************************************************
* @brief                    Setup connect() syscall environment
* @param [in] pOps          Pointer to connect() operation environment
* @return                   Pointer to SfNetworkEnvironment on success, NULL otherwise
****************************************************************************************************
*/
static SfProtocolHeader* SetupNetworkEnvironment( const SfProtocolHeader* pOps )
{
    SfProtocolHeader* pEnvHeader = NULL;
    const SfOperationSocketConnect* pArgs = (const SfOperationSocketConnect*)pOps;
    if ( AF_INET != pArgs->pAddress->sa_family )
        goto out;

    pEnvHeader = SF_CREATE_ENVIRONMENT( SfNetworkEnvironment, SF_ENVIRONMENT_TYPE_NETWORK );
    if ( pEnvHeader )
    {
        SfNetworkEnvironment* pEnv = (SfNetworkEnvironment*)pEnvHeader;
        if ( SF_FAILED(SfdFillExecutionEnvironment( &pEnv->processContext, current, GetTime(),
                                                    pArgs->result )) ||
             SF_FAILED(SfdFillNetworkEnvironment( pEnv, pArgs )) )
        {
            SfDestroyNetworkEnvironment( pEnv );
            pEnvHeader = NULL;
        }
    }
    else
        SF_LOG_E( "%s(): failed to create network environment", __FUNCTION__ );
out:
    return pEnvHeader;
}

/**
****************************************************************************************************
* @brief                    Setup and send notification packet with syscall params to userspace
* @param [in] pPacket       Event packet
* @param [in] envCb         Packet environment setup callback
* @return                   SF_STATUS_OK
****************************************************************************************************
*/
static SF_STATUS SendNotificationPacket( SfPacket* pPacket, SetupEnvCallback envCb )
{
    // if environment is empty, try to create it
    if ( !pPacket->env )
        pPacket->env = envCb( pPacket->op );

    // if environment has been created or it was here, send notification
    if ( pPacket->env )
        SfSendPacket( g_pDispatcher->module.pNode, pPacket );

    return SF_STATUS_OK;
}

/**
****************************************************************************************************
* @brief                    Check if notifier will handle this packet, obtain environment setup
*                           callback
* @param [in] pPacket       Event packet
* @param [in,out] pEnvCb    Packet environment setup callback
* @return                   TRUE if packet type is supported, FALSE otherwise
****************************************************************************************************
*/
static Bool CheckPacketType( SfPacket* pPacket, SetupEnvCallback* pEnvCb )
{
    Bool r = TRUE;
    switch ( pPacket->op->type )
    {
        case SF_OPERATION_TYPE_OPEN:
            *pEnvCb = &SetupOpenEnvironment;
            break;

        case SF_OPERATION_TYPE_CONNECT:
            *pEnvCb = &SetupNetworkEnvironment;
            break;

        case SF_OPERATION_TYPE_EXEC:
            *pEnvCb = &SetupExecveEnvironment;
            break;

        case SF_OPERATION_TYPE_MMAP:
            *pEnvCb = &SetupMmapEnvironment;
            break;

        default:
            *pEnvCb = NULL;
            r = FALSE;
            break;
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdNotifierPacketHandler( const SfProtocolHeader* const pPacketInterface )
{
    // do not notify any event with SFD before 1 min from system boot
    const unsigned long eventNotificationDelay = 1 * 60 * HZ;
    SfPacket* pPacket = NULL;
    SetupEnvCallback envCb = NULL;
    SF_STATUS result = SF_STATUS_OK;

    if ( time_before( jiffies, eventNotificationDelay ) )
        goto out;

    result = SF_VALIDATE_PACKET( pPacketInterface );
    if ( SF_FAILED( result ) )
        goto out;

    pPacket = (SfPacket*)pPacketInterface;
    result = SF_VALIDATE_OPERATION( pPacket->op );
    if ( SF_FAILED( result ) )
        goto out;

    if ( CheckPacketType( pPacket, &envCb ) )
        result = SendNotificationPacket( pPacket, envCb );
    else
        result = SF_STATUS_NOT_IMPLEMENTED;
out:
    return result;
}