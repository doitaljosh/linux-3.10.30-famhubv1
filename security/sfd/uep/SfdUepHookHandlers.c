/**
****************************************************************************************************
* @file SfdUepHookHandlers.c
* @brief Security framework [SF] filter driver [D] hook handlers for system calls that processed by
*	UEP submodule.
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
#include <uapi/linux/sf/core/SfCore.h>
#include <uapi/linux/sf/protocol/SfEnvironmentFormat.h>
#include <uapi/linux/sf/protocol/SfOperationsFormat.h>

#include "uep/SfdUepHookHandlers.h"
#include "uep/UepConfig.h"
#include "uep/UepKey.h"
#include "uep/UepSignatureContext.h"
#include "uep/SfdUepCache.h"

#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/elf.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>

//--------------------------------------------------------------------------------------------------
#define HASH_ALGO_NAME   "md5"
#define HASH_ALGO_LENGTH 16
//--------------------------------------------------------------------------------------------------
static const Char* pubkeyN = "0x00b61e6eef650cd6b2cb8f177228e5"
                             "3ce99254496f6e83d4a9510cdd79d8"
                             "415c478baa130117f2bab568fd67fa"
                             "7d66b5d98f2d8819f62a49dbe53ce2"
                             "fc4c86cd95b234e1819a96e3aa0335"
                             "084f142dab9d1fe5c28c704d9da2b3"
                             "8c2b13d649b2e4bf0711b90ea9263d"
                             "e16ee826c0fd951c8a6dafaa6c5966" 
                             "b02dfc61fcf69fc819";
static const Char* pubkeyE = "0x10001";
//--------------------------------------------------------------------------------------------------

Int s_uepStatus = 1;
static DECLARE_RWSEM(s_uepRwsem);
static Uint8 s_duidHash[ HASH_ALGO_LENGTH ] = { 0 };

//--------------------------------------------------------------------------------------------------

typedef struct
{
    Char*   signature;            ///< Null-terminated signature in hex
    Uint32  signatureLength;      ///< Signature length
    Uint32  signatureOffset;      ///< Signature offset in file
} UepSignatureInfo;

/**
****************************************************************************************************
* @brief                Get file size from inode structure
* @param [in] pFile     Pointer to the file
* @return               Value returned by the i_size_read(struct inode*) function
****************************************************************************************************
*/
static inline Uint64 SfdUepGetFileSize( const struct file* pFile )
{
    return i_size_read( pFile->f_inode );
}

/**
****************************************************************************************************
* @brief                Read data from file
* @param [in] pFile     Pointer to the file
* @param [in] offset    Offset to read file
* @param [out] pBuffer  Pointer to the output buffer
* @param [in] size      Size of the file to read
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_STATUS SfdUepReadFile( struct file* pFile, unsigned long offset, unsigned char* pBuffer,
                                 unsigned long size )
{
    Int readSize = kernel_read( pFile, offset, pBuffer, size );
    return ( readSize == size ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/**
****************************************************************************************************
* @brief                Convert big endian number to little endian
* @param [in] pData     Pointer to the input data to convert
* @return               Number in little endian format
****************************************************************************************************
*/
static inline Uint32 SfdUepBigToLittle( const Uint8* pData )
{
    return ((Uint32)pData[0] << 24) |
           ((Uint32)pData[1] << 16) |
           ((Uint32)pData[2] << 8 ) |
           ((Uint32)pData[3]);
}

/**
****************************************************************************************************
* @brief                Check UEP signature MAGIC number
* @param [in] pData     Pointer to data to check magic number
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static inline SF_STATUS SfdUepCheckMagicNumber( const Uint8* pData )
{
    return ( (pData[0] == SFD_UEP_SIGN_MAG0) &&
             (pData[1] == SFD_UEP_SIGN_MAG1) &&
             (pData[2] == SFD_UEP_SIGN_MAG2) &&
             (pData[3] == SFD_UEP_SIGN_MAG3) ) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/**
****************************************************************************************************
* @brief                Read signature from file
* @param [in] pFile     Pointer to the file
* @param [in] fileSize  Size of of file
* @param [out] pInfo    Pointer to the signature context
* @return               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_STATUS SfdUepReadSignatureFromFile( struct file* pFile, Uint64 fileSize,
                                              UepSignatureInfo* pInfo )
{
    SF_STATUS result = SF_STATUS_OK;
    Uint8 signatureMagic[ 4 ] = { 0 };

    do
    {
        /**
        * @note Read signature magic number
        */
        result = SfdUepReadFile(pFile, fileSize - 4, signatureMagic, 4);
        if (SF_FAILED(result))
        {
            SF_LOG_E("%s: Can not read data from file", __FUNCTION__);
            break;
        }

        result = SfdUepCheckMagicNumber(signatureMagic);
        if (SF_FAILED(result))
        {
            break;
        }

        /**
        * @note Now signatureMagic used to read signature size
        */
        result = SfdUepReadFile(pFile, fileSize - 8, signatureMagic, 4);
        if (SF_FAILED(result))
        {
            break;
        }

        pInfo->signatureLength = SfdUepBigToLittle(signatureMagic);
        pInfo->signatureOffset = fileSize - pInfo->signatureLength - 8;

        /**
        * @note NULL symbol included into size of signature. Since, signature
        *   stored in string format.
        */
        pInfo->signature = (Char*) sf_malloc(pInfo->signatureLength);
        if (NULL == pInfo->signature)
        {
            break;
        }

        result = SfdUepReadFile(pFile, pInfo->signatureOffset, pInfo->signature,
            pInfo->signatureLength);
        if (SF_FAILED(result))
        {
            sf_free(pInfo->signature);
            pInfo->signature = NULL;
            break;
        }

        result = SF_STATUS_OK;

    } while(FALSE);

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS HashFile( struct file* pFile, UepSignatureContext* pCtx, Uint32 offset )
{
    SF_STATUS r = SF_STATUS_OK;
    const Uint32 dataSize = 16 * 1024;
    Uint32 rOffset = 0;
    unsigned char* pData = NULL;

    pData = (unsigned char*)sf_malloc( dataSize );
    if ( !pData )
    {
        return SF_STATUS_FAIL;
    }

    SignatureInit( pCtx );
    while ( offset )
    {
        Uint32 toRead = ( dataSize < offset ) ? dataSize : offset;
        if ( SF_FAILED(SfdUepReadFile( pFile, rOffset, pData, toRead )) )
        {
            r = SF_STATUS_FAIL;
            break;
        }
        SignatureUpdate( pCtx, pData, toRead );
        offset  -= toRead;
        rOffset += toRead;
    }
    sf_free( pData );
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepPacketHandler(const SfProtocolHeader* const pPacketInterface)
{
    SF_STATUS result = SF_STATUS_OK;

    down_read( &s_uepRwsem );
    do
    {
        SfPacket* pCurrentPacket = NULL;
        if ( !s_uepStatus )
        {
            // UEP is disabled
            result = SF_STATUS_OK;
            break;
        }

        result = SF_VALIDATE_PACKET(pPacketInterface);
        if ( SF_FAILED(result) )
            break;

        pCurrentPacket = (SfPacket*)pPacketInterface;
        result = SF_VALIDATE_OPERATION( pCurrentPacket->op );
        if ( SF_FAILED(result) )
            break;

        switch ( pCurrentPacket->op->type )
        {
            case SF_OPERATION_TYPE_MMAP:
                result = SfdUepVerifyDynamicLibrary( pCurrentPacket->op );
                break;

            case SF_OPERATION_TYPE_EXEC:
                result = SfdUepVerifyExecutableBinary( pCurrentPacket->op );
                break;

            default:
                result = SF_STATUS_NOT_IMPLEMENTED;
                break;
        }
    } while ( FALSE );
    up_read( &s_uepRwsem );

    if ( result == SF_STATUS_UEP_SIGNATURE_DUID )
    {
        // disable UEP because of DUID hash match
        down_write( &s_uepRwsem );
        s_uepStatus = 0;
        up_write( &s_uepRwsem );
        SF_LOG_I( "%s(): UEP has been disabled", __FUNCTION__ );
    }
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyDynamicLibrary(const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationFileMmap* pCurrentOperation = NULL;

    SF_ASSERT( SF_DEBUG_CLASS_UEP, pOperationInterface->size == sizeof(SfOperationFileMmap),
               "%s got invalid packet", __FUNCTION__ );

    pCurrentOperation = (SfOperationFileMmap*)pOperationInterface;
    if ( pCurrentOperation->prot & PROT_EXEC )
    {
        result = SfdUepVerifyFileSignature( pCurrentOperation->pFile );
        pCurrentOperation->result = result;
    }
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyExecutableBinary(const SfProtocolHeader* const pOperationInterface)
{
    SF_STATUS result = SF_STATUS_OK;
    SfOperationBprmCheckSecurity* pCurrentOperation = NULL;

    SF_ASSERT( SF_DEBUG_CLASS_UEP, pOperationInterface->size == sizeof(SfOperationBprmCheckSecurity),
               "%s got invalid operation", __FUNCTION__ );

    pCurrentOperation = (SfOperationBprmCheckSecurity*)pOperationInterface;
    result = SfdUepVerifyFileSignature( pCurrentOperation->pBinParameters->file );
    pCurrentOperation->result = result;
    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdUepCheckFileSignature(struct file* const pFile, UepSignatureInfo* const pInfo)
{
    SF_STATUS result = SF_STATUS_FAIL;
    UepSignatureContext ctx = { .hashCtx = { 0 } };

    do
    {
        UepKey* pKey = NULL;
        result = SetupSignatureContext( &ctx );
        if ( SF_FAILED( result ) )
        {
            SF_LOG_E( "%s(): failed to setup signature context", __FUNCTION__ );
            break;
        }

        result = HashFile( pFile, &ctx, pInfo->signatureOffset );
        if ( SF_FAILED( result ) )
        {
            SF_LOG_E("%s(): failed to hash file", __FUNCTION__);
            break;
        }

        pKey = CreateKey( pubkeyN, pubkeyE );
        if ( !pKey )
        {
            SF_LOG_E( "%s(): failed to create RSA key", __FUNCTION__ );
            break;
        }

        result = SignatureVerify( &ctx, pKey, pInfo->signature, pInfo->signatureLength, s_duidHash );
        DestroyKey( pKey );
    } while( FALSE );

    FreeSignatureContext( &ctx );
    return result;
}

/**
****************************************************************************************************
* @brief                Print message about UEP verification routine
* @param   [in] pFile   Pointer to the file was processed
* @param   [in] result  Result was returned from UEP verification routine
* @warning              Print messages only in debug and release mode
* @return
****************************************************************************************************
*/
static void SfdUepHandleVerificationResult(struct file* const pFile, SF_STATUS result)
{
#if defined(CONFIG_SECURITY_SFD_LEVEL_DEBUG)
    Char* pName = NULL;
    Char* pBuffer = SfConstructAbsoluteFileNameByFile( pFile, &pName );
    if ( !pBuffer )
    {
        SF_LOG_E( "%s(): failed to construct file name", __FUNCTION__ );
        return;
    }

    switch ( result )
    {
        case SF_STATUS_UEP_FILE_NOT_SIGNED:
            SF_LOG_I( "%s(): file %s is not signed. Sign it", __FUNCTION__, pName );
            break;

        case SF_STATUS_UEP_SIGNATURE_CORRECT:
            SF_LOG_I( "%s(): file %s has correct signature", __FUNCTION__, pName );
            break;

        case SF_STATUS_UEP_SIGNATURE_INCORRECT:
            SF_LOG_I( "%s(): file %s has incorrect signature", __FUNCTION__, pName );
            break;

        case SF_STATUS_FAIL:
            SF_LOG_E( "%s(): signature general check error on file %s", __FUNCTION__, pName );
            break;

        case SF_STATUS_OK:
            SF_LOG_I( "%s(): signature verification skipped. %s was cached", __FUNCTION__, pName );
            break;

        case SF_STATUS_UEP_FILE_IS_NOT_ELF:
            SF_LOG_I( "%s(): file %s is not ELF", __FUNCTION__, pName );
            break;

        default:
            SF_LOG_E( "%s(): unexpected result: %d", __FUNCTION__, result );
            break;
    }
    sf_free( pBuffer );
#else
#endif  // CONFIG_SECURITY_SFD_LEVEL_DEBUG
}

/**
****************************************************************************************************
* @brief Check the ELF identification
* @warning This function doesn't check for NULL pointer. The function expect that buffer will be
*   not NULL.
* @return SF_STATUS_OK on ELF file identification magic numbers, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_STATUS SfdUepElfIdentificationCheck(const char* const pBuffer)
{
    return (pBuffer[EI_MAG0] == ELFMAG0 &&
            pBuffer[EI_MAG1] == ELFMAG1 &&
            pBuffer[EI_MAG2] == ELFMAG2 &&
            pBuffer[EI_MAG3] == ELFMAG3) ? SF_STATUS_OK : SF_STATUS_FAIL;
}

/**
****************************************************************************************************
* @brief Performs checking of the file type. ELF file has the following magic number identification
*  0x7f, 'E', 'L', 'F'.
* @return SF_STATUS_OK in case if file is ELF format, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
static SF_STATUS SfdUepCheckFileTypeIsELF(struct file* const pFile)
{
    SF_STATUS result = SF_STATUS_OK;
    char eIdent[4];

    do
    {
        result = SfdUepReadFile(pFile, 0, eIdent, sizeof(eIdent));
        if (SF_FAILED(result))
        {
            SF_LOG_E("%s: Can't read ELF identification number", __FUNCTION__);
            break;
        }

        result = SfdUepElfIdentificationCheck(eIdent);
        if (SF_FAILED(result))
        {
            result = SF_STATUS_UEP_FILE_IS_NOT_ELF;
            break;
        }

    } while(FALSE);

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfdUepVerifyFileSignature(struct file* const pFile)
{
    SF_STATUS result = SF_STATUS_OK;
    UepSignatureInfo info;

    do
    {

        result = SfdUepCheckFileTypeIsELF(pFile);
        if (SF_STATUS_UEP_FILE_IS_NOT_ELF == result)
        {
            /**
            * @brief This case is for checking ELF magic number. When file is
            *   ELF function return success and UEP routine must be executed.
            *   In case of File is not ELF it must be skiped. SF_STATUS_OK will
            *   be returned to pass normal security check result for not ELF files.
            */
            break;
        }

        result = SfdUepCheckCachedData(pFile);
        if (SF_SUCCESS(result))
        {
            break;
        }

        result = SfdUepReadSignatureFromFile(pFile, SfdUepGetFileSize(pFile), &info);
        if (SF_FAILED(result))
        {
            result = SF_STATUS_UEP_FILE_NOT_SIGNED;
            break;
        }

        result = SfdUepCheckFileSignature(pFile, &info);

        /**
        * @brief Added cached data for files with correct signature
        */
        if (SF_STATUS_UEP_SIGNATURE_CORRECT == result)
        {
            SfdUepAddCacheData(pFile);
        }

    } while(FALSE);

    /**
    * @note This function print logs. It will be changed changed by the static inline empty stub,
    *   when kernel will be compiled in the release mode.
    */
    SfdUepHandleVerificationResult(pFile, result);

    return result;
}

//--------------------------------------------------------------------------------------------------

static SF_STATUS HashDuid( const Char* duid, Uint8* out )
{
    struct hash_desc desc;
    struct scatterlist sg;
    Uint duidLength = 0;

    desc.flags = 0;
    desc.tfm   = crypto_alloc_hash( HASH_ALGO_NAME, 0, CRYPTO_ALG_ASYNC );
    if ( IS_ERR( desc.tfm ) )
    {
        SF_LOG_E( "%s(): failed to allocate %s hashing algorithm", __FUNCTION__, HASH_ALGO_NAME );
        return SF_STATUS_FAIL;
    }

    duidLength = strlen( duid );
    sg_init_one( &sg, (Uint8*)duid, duidLength );
    crypto_hash_digest( &desc, &sg, duidLength, out );
    crypto_free_hash( desc.tfm );
    return SF_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

SF_STATUS SetupDuidHash( const Char* duid )
{
    SF_STATUS r = SF_STATUS_FAIL;
    if ( !duid )
    {
        SF_LOG_E( "%s(): input is NULL", __FUNCTION__ );
        return SF_STATUS_BAD_ARG;
    }

    down_write( &s_uepRwsem );
    r = HashDuid( duid, s_duidHash );
    up_write( &s_uepRwsem );
    if ( SF_SUCCESS( r ) )
        SF_LOG_I( "%s(): hash of DUID [%s] has been set", __FUNCTION__, duid );
    return r;
}

//--------------------------------------------------------------------------------------------------
