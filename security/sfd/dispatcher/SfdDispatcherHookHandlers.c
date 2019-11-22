/**
****************************************************************************************************
* @file SfdDispatcherHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handler for system calls implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Dmitriy Dorogovtsev (d.dorogvtse@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 10, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include "SfdDispatcher.h"
#include "SfdConfiguration.h"

#include <linux/in.h>
#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/jiffies.h>

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_OPEN)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_file_open( struct file* pFile, const struct cred* pCredentials )
{
    SF_STATUS result = SF_STATUS_OK;
    const Ulong openEventFilterDelay = 5 * 60 * HZ;

    // do not filter open() event with SFD before 5 min from system boot
    if ( time_before( jiffies, openEventFilterDelay ) )
        return 0;

    if ( pFile && pCredentials )
    {
        result = SfCheckModuleResponsibility( pFile );
        if ( SF_SUCCESS( result ) )
        {
            SfOperationFileOpen args =
            {
                .header =
                {
                    .size = sizeof(SfOperationFileOpen),
                    .type = SF_OPERATION_TYPE_OPEN
                },
                .pFile = pFile,
                .pCred = (struct cred*)pCredentials
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_EXEC)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_bprm_check( struct linux_binprm* pBinaryParameters )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pBinaryParameters && pBinaryParameters->file )
    {
        result = SfCheckModuleResponsibility( pBinaryParameters->file );
        if ( SF_SUCCESS( result ) )
        {
            SfOperationBprmCheckSecurity args =
            {
                .header =
                {
                    .size = sizeof(SfOperationBprmCheckSecurity),
                    .type = SF_OPERATION_TYPE_EXEC
                },
                .pBinParameters = pBinaryParameters
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_MMAP)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_mmap_file( struct file* pFile, unsigned long prot, unsigned long flags )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pFile )
    {
        result = SfCheckModuleResponsibility( pFile );
        if ( SF_SUCCESS( result ) )
        {
            SfOperationFileMmap args =
            {
                .header =
                {
                    .size = sizeof(SfOperationFileMmap),
                    .type = SF_OPERATION_TYPE_MMAP
                },
                .pFile = pFile,
                .prot  = prot,
                .flags = flags
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_LOAD_KERNEL_MODULE)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_kernel_module_from_file( struct file* pFile )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pFile )
    {
        // mmap() operation with PROT_EXEC is chosen here so
        // UEP will be able to verify kernel modules
        result = SfCheckModuleResponsibility( pFile );
        if ( SF_SUCCESS( result ) )
        {
            SfOperationFileMmap args =
            {
                .header =
                {
                    .size = sizeof(SfOperationFileMmap),
                    .type = SF_OPERATION_TYPE_MMAP
                },
                .pFile = pFile,
                .prot  = PROT_EXEC
            };
            result = SfdProcessOperationThroughModules( &args.header );
        }
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_SOCKET)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_create( int family, int type, int protocol, int kernel )
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationSocketCreate args =
    {
        .header =
        {
            .size = sizeof(SfOperationSocketCreate),
            .type = SF_OPERATION_TYPE_SOCKET
        },
        .family   = family,
        .type     = type,
        .protocol = protocol,
        .kernel   = kernel
    };
    result = SfdProcessOperationThroughModules( &args.header );
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_BIND)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_bind( struct socket* pSocket, struct sockaddr* pAddress, int addrlen )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pAddress )
    {
        SfOperationSocketBind args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketBind),
                .type = SF_OPERATION_TYPE_BIND
            },
            .pSocket       = pSocket,
            .pAddress      = pAddress,
            .addressLength = addrlen
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
};
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_CONNECT)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_connect( struct socket* pSocket, struct sockaddr* pAddress, int addrlen )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pAddress && ( AF_INET == pAddress->sa_family ) )
    {
        SfOperationSocketConnect args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketConnect),
                .type = SF_OPERATION_TYPE_CONNECT
            },
            .pSocket       = pSocket,
            .pAddress      = pAddress,
            .addressLength = addrlen
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_LISTEN)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_listen( struct socket* pSocket, int backlog )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket )
    {
        SfOperationSocketListen args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketListen),
                .type = SF_OPERATION_TYPE_LISTEN
            },
            .pSocket = pSocket,
            .backLog = backlog
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif

#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_ACCEPT)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
int sf_security_socket_accept( struct socket* pSocket, struct socket* pNewSocket )
{
    SF_STATUS result = SF_STATUS_OK;
    if ( pSocket && pNewSocket )
    {
        SfOperationSocketAccept args =
        {
            .header =
            {
                .size = sizeof(SfOperationSocketAccept),
                .type = SF_OPERATION_TYPE_ACCEPT
            },
            .pSocket    = pSocket,
            .pNewSocket = pNewSocket
        };
        result = SfdProcessOperationThroughModules( &args.header );
    }
    return SfReturn( result );
}
#endif
