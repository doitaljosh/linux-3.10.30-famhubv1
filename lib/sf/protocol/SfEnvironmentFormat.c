/**
****************************************************************************************************
* @file SfdEnvironmentFormat.c
* @brief Security framework [SF] filter driver [D] environment format structure implementation
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 24, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/protocol/SfEnvironmentFormat.h>

#if defined(SF_LEVEL_KERNEL)
#include <linux/sched.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/binfmts.h>

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillExecutionEnvironment( SfExecutionEnvironmentInfo* pEnvironment,
                                             struct task_struct* pProcessContext, Uint64 time,
                                             Int32 result )
{
    if ( !pEnvironment || !pProcessContext )
    {
        SF_LOG_E( "%s takes invalid argument (pEnvironment = %p), pProcessContext = %p",
                  __FUNCTION__, pEnvironment, pProcessContext );
        return SF_STATUS_BAD_ARG;
    }

    pEnvironment->processId      = pProcessContext->pid;
    pEnvironment->timeStamp      = time;
    pEnvironment->sysCallResult  = result;
    pEnvironment->pBuffer = SfConstructAbsoluteFileNameByTask( pProcessContext,
                                                               &pEnvironment->pProcessName );

    return ( pEnvironment->pBuffer ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillFileEnvironment( SfFileEnvironment* pEnvironment,
                                        const SfOperationFileOpen* pArgs )
{
    if ( !pEnvironment || !pArgs )
    {
        SF_LOG_E( "%s takes invalid argument (pEnvironment = %p), pArgs = %p", __FUNCTION__,
                  pEnvironment, pArgs );
        return SF_STATUS_BAD_ARG;
    }

    pEnvironment->inode = pArgs->pFile->f_dentry->d_inode->i_ino;
    pEnvironment->pBuffer = SfConstructAbsoluteFileNameByFile( pArgs->pFile,
                                                               &pEnvironment->pFileName );
    return ( pEnvironment->pBuffer ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillProcessEnvironment( SfProcessEnvironment* pEnvironment,
                                           const SfOperationBprmCheckSecurity* pArgs )
{
    struct file* pFile = NULL;
    if ( !pEnvironment || !pArgs )
    {
        SF_LOG_E( "%s takes invalid argument (pEnvironment = %p), pArgs = %p", __FUNCTION__,
                  pEnvironment, pArgs );
        return SF_STATUS_BAD_ARG;
    }

    pFile = pArgs->pBinParameters->file;
    pEnvironment->processImageId = pFile->f_dentry->d_inode->i_ino;
    pEnvironment->pBuffer = SfConstructAbsoluteFileNameByFile( pFile, &pEnvironment->pProcessName );
    return ( pEnvironment->pBuffer ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillNetworkEnvironment( SfNetworkEnvironment* pEnvironment,
                                           const SfOperationSocketConnect* pArgs )
{
    struct sockaddr_in* pInAddr = NULL;
    if ( !pEnvironment || !pArgs )
    {
        SF_LOG_E( "%s takes invalid argument (pEnvironment = %p), pArgs = %p", __FUNCTION__,
                  pEnvironment, pArgs );
        return SF_STATUS_BAD_ARG;
    }

    pInAddr = (struct sockaddr_in*)( pArgs->pAddress );
    pEnvironment->addr = pInAddr->sin_addr.s_addr;
    pEnvironment->port = pInAddr->sin_port;
    return SF_STATUS_OK;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillMmapEnvironment( SfMmapEnvironment* pEnvironment,
                                        const SfOperationFileMmap* pArgs )
{
    struct file* pFile = NULL;
    if ( !pEnvironment || !pArgs )
    {
        SF_LOG_E( "%s(): invalid arguments pEnvironment = %p, pArgs = %p", __FUNCTION__,
                  pEnvironment, pArgs );
        return SF_STATUS_BAD_ARG;
    }

    pFile = pArgs->pFile;
    pEnvironment->inode = pFile->f_dentry->d_inode->i_ino;
    pEnvironment->pBuffer = SfConstructAbsoluteFileNameByFile( pFile, &pEnvironment->pLibraryName );
    return ( pEnvironment->pBuffer ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

#endif  /* SF_LEVEL_KERNEL */

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static void DestroyExecutionEnvironment( SfExecutionEnvironmentInfo* pEnv )
{
    if ( pEnv )
    {
#if defined(SF_LEVEL_KERNEL)
        if ( pEnv->pBuffer )
            sf_free( pEnv->pBuffer );
#else
        if ( pEnv->pProcessName )
            sf_free( pEnv->pProcessName );
#endif  // SF_LEVEL_KERNEL
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyFileEnvironment( SfFileEnvironment* pEnv )
{
    if ( pEnv )
    {
#if defined(SF_LEVEL_KERNEL)
        if ( pEnv->pBuffer )
            sf_free( pEnv->pBuffer );
#else
        if ( pEnv->pFileName )
            sf_free( pEnv->pFileName );
#endif  // SF_LEVEL_KERNEL
        DestroyExecutionEnvironment( &pEnv->processContext );
        sf_free( pEnv );
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyProcessEnvironment( SfProcessEnvironment* pEnv )
{
    if ( pEnv )
    {
#if defined(SF_LEVEL_KERNEL)
        if ( pEnv->pBuffer )
            sf_free( pEnv->pBuffer );
#else
        if ( pEnv->pProcessName )
            sf_free( pEnv->pProcessName );
#endif  // SF_LEVEL_KERNEL
        DestroyExecutionEnvironment( &pEnv->processContext );
        sf_free( pEnv );
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyNetworkEnvironment( SfNetworkEnvironment* pEnv )
{
    if ( pEnv )
    {
        DestroyExecutionEnvironment( &pEnv->processContext );
        sf_free( pEnv );
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyMmapEnvironment( SfMmapEnvironment* pEnv )
{
    if ( pEnv )
    {
#if defined(SF_LEVEL_KERNEL)
        if ( pEnv->pBuffer )
            sf_free( pEnv->pBuffer );
#else
        if ( pEnv->pLibraryName )
            sf_free( pEnv->pLibraryName );
#endif  // SF_LEVEL_KERNEL
        DestroyExecutionEnvironment( &pEnv->processContext );
        sf_free( pEnv );
    }
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyEnvironment( SfProtocolHeader* pHeader )
{
    if ( pHeader )
    {
        switch ( pHeader->type )
        {
            case SF_ENVIRONMENT_TYPE_FILE:
                SfDestroyFileEnvironment( (SfFileEnvironment*)pHeader );
                break;

            case SF_ENVIRONMENT_TYPE_PROCESS:
                SfDestroyProcessEnvironment( (SfProcessEnvironment*)pHeader );
                break;

            case SF_ENVIRONMENT_TYPE_NETWORK:
                SfDestroyNetworkEnvironment( (SfNetworkEnvironment*)pHeader );
                break;

            case SF_ENVIRONMENT_TYPE_MMAP:
                SfDestroyMmapEnvironment( (SfMmapEnvironment*)pHeader );
                break;
        }
    }
}