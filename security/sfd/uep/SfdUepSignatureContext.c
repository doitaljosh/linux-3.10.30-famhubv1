#include "uep/UepSignatureContext.h"

#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfMemory.h>

#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/err.h>

#define HASH_LENGTH 16      ///< MD5 hash length in bytes

static const Uint8 RSA_MD5_der_enc [] = { 0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                          0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10 };

/*
****************************************************************************************************
* @note     Signature verification process is done according to PKCS#1 v2.1, EMSA-PKCS1-v1_5. \n
*           http://www.rfc-base.org/txt/rfc-3447.txt \n
*           Signature format: \n
*                   0x00|0x01|0xFF|...|0xFF|0x00|[DER encoding of hash function]|[Hash value] \n
*           MPI bigint library drops heading 0x00 byte, so check starts from 0x01
****************************************************************************************************
*/
static SF_STATUS VerifyEncodedMessage( const Uint8* hash,  Uint32 hashLength,
                                       const Uint8* em, Uint32 emLength,
                                       const Uint8* der, Uint32 derLength,
                                       const Uint8* duidHash )
{
    Uint32 PS_end = 0, i = 0;

    if ( emLength < ( derLength + hashLength + 2 ) )
    {
        return SF_STATUS_FAIL;
    }

    if ( 0x01 != em[ 0 ] )
    {
        SF_LOG_E( "%s(): Bad signature format: S[ 0 ] = 0x%02X != 0x01", __FUNCTION__, em[0] );
        return SF_STATUS_UEP_SIGNATURE_INCORRECT;
    }

    PS_end = emLength - (derLength + hashLength) - 1;
    if ( 0x00 != em[PS_end] )
    {
        SF_LOG_E( "%s(): Bad signature format: S[ PS_end ] = 0x%02X != 0x00", __FUNCTION__,
                  em[PS_end] );
        return SF_STATUS_UEP_SIGNATURE_INCORRECT;
    }

    for ( i = 1; i < PS_end; ++i )
    {
        if ( 0xff != em[i] )
        {
            SF_LOG_E( "%s(): Bad signature format: S[ %d ] = 0x%02X != 0xFF", __FUNCTION__,
                      i, em[i] );
            return SF_STATUS_UEP_SIGNATURE_INCORRECT;
        }
    }

    if ( memcmp( der, em + PS_end + 1, derLength ) )
    {
        SF_LOG_E( "%s(): Signature check failed: DER encoding mismatch", __FUNCTION__ );
        return SF_STATUS_UEP_SIGNATURE_INCORRECT;
    }

    if ( memcmp( hash, em + PS_end + 1 + derLength, hashLength ) )
    {
        SF_LOG_E( "%s(): Signature check failed: hash value mismatch", __FUNCTION__ );
        if ( !memcmp( duidHash, em + PS_end + 1 + derLength, HASH_LENGTH ) )
        {
            SF_LOG_I( "%s(): DUID hash match", __FUNCTION__ );
            return SF_STATUS_UEP_SIGNATURE_DUID;
        }
        return SF_STATUS_UEP_SIGNATURE_INCORRECT;
    }

    return SF_STATUS_UEP_SIGNATURE_CORRECT;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS CheckSignature( const UepKey* pKey, const Uint8* signature, Uint32 sLength,
                                 const Uint8* hash, Uint32 hLength, const Uint8* duidHash )
{
    SF_STATUS r = SF_STATUS_FAIL;
    MPI mSign = NULL, mEm = NULL;

    do
    {
        Uint8* em = NULL;
        Uint32 emLength = 0;

        mSign = mpi_alloc( 0 ); mEm = mpi_alloc( 0 );
        if ( !mSign || !mEm )
        {
            break;
        }
        if ( mpi_fromstr( mSign, signature ) )
        {
            SF_LOG_E( "%s(): failed to initialize MPI from signature %s", __FUNCTION__, signature );
            break;
        }
        if ( mpi_powm( mEm, mSign, pKey->rsaE, pKey->rsaN ) )
        {
            SF_LOG_E( "%s(): failed to perform modular exponentiation", __FUNCTION__ );
            break;
        }
        em = mpi_get_buffer( mEm, &emLength, NULL );
        if ( !em )
        {
            SF_LOG_E( "%s(): failed to get MPI buffer", __FUNCTION__ );
            break;
        }

        r = VerifyEncodedMessage( hash, hLength, em, emLength, RSA_MD5_der_enc,
                                  sizeof(RSA_MD5_der_enc), duidHash );
        sf_free( em );
    }
    while ( FALSE );

    mpi_free( mSign );
    mpi_free( mEm );
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SetupSignatureContext( UepSignatureContext* pCtx )
{
    SF_STATUS r = SF_STATUS_FAIL;
    if ( pCtx )
    {
        pCtx->hashCtx.flags = 0;
        pCtx->hashCtx.tfm   = crypto_alloc_hash( "md5", 0, CRYPTO_ALG_ASYNC );
        if ( IS_ERR( pCtx->hashCtx.tfm ) )
        {
            SF_LOG_E( "%s(): failed to allocate md5 hash algorithm", __FUNCTION__ );
            pCtx->hashCtx.tfm = NULL;
        }
        else
        {
            r = SF_STATUS_OK;
        }
    }
    else
    {
        SF_LOG_E( "%s(): pCtx is NULL", __FUNCTION__ );
        r = SF_STATUS_BAD_ARG;
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SignatureInit( UepSignatureContext* pCtx )
{
    SF_STATUS r = SF_STATUS_BAD_ARG;
    if ( pCtx )
    {
        crypto_hash_init( &pCtx->hashCtx );
        r = SF_STATUS_OK;
    }
    else
    {
        SF_LOG_E( "%s(): pCtx is NULL", __FUNCTION__ );
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SignatureUpdate( UepSignatureContext* pCtx, const Uint8* pData, Uint32 length )
{
    SF_STATUS r = SF_STATUS_BAD_ARG;
    if ( pCtx && pData )
    {
        struct scatterlist sg;
        sg_init_one( &sg, pData, length );
        crypto_hash_update( &pCtx->hashCtx, &sg, length );
        r = SF_STATUS_OK;
    }
    else
    {
        SF_LOG_E( "%s(): pCtx or data is NULL", __FUNCTION__ );
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SignatureVerify( UepSignatureContext* pCtx, const UepKey* pKey, const Uint8* pSignature,
                           Uint32 length, const Uint8* duidHash )
{
    SF_STATUS r = SF_STATUS_BAD_ARG;
    Uint8 hash [ HASH_LENGTH ] = { 0 };
    if ( pCtx && pKey && pSignature )
    {
        crypto_hash_final( &pCtx->hashCtx, hash );
        r = CheckSignature( pKey, pSignature, length, hash, HASH_LENGTH, duidHash );
    }
    else
    {
        SF_LOG_E( "%s(): pCtx or pKey or pSignature is NULL", __FUNCTION__ );
    }
    return r;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void FreeSignatureContext( UepSignatureContext* pCtx )
{
    if ( pCtx && pCtx->hashCtx.tfm )
    {
        crypto_free_hash( pCtx->hashCtx.tfm );
    }
}