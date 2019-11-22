#ifndef _UEP_SIGNATURE_CONTEXT_H_
#define _UEP_SIGNATURE_CONTEXT_H_

#include "uep/UepKey.h"
#include <uapi/linux/sf/core/SfStatus.h>

#include <linux/crypto.h>

typedef struct
{
    struct hash_desc hashCtx;
} UepSignatureContext;

/**
****************************************************************************************************
* @brief                    Setup context
* @param [in] pCtx          Signature context
* @return                   SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SetupSignatureContext( UepSignatureContext* pCtx );

/**
****************************************************************************************************
* @brief                    Initialize signature verification context
* @param [in] pCtx          Signature context
* @return                   SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SignatureInit( UepSignatureContext* pCtx );

/**
****************************************************************************************************
* @brief                    Process portion of data during signature verification
* @param [in] pCtx          Signature context
* @param [in] pData         Data
* @param [in] length        Data length in bytes
* @return                   SF_STATUS_OK on success
****************************************************************************************************
*/
SF_STATUS SignatureUpdate( UepSignatureContext* pCtx, const Uint8* pData, Uint32 length );

/**
****************************************************************************************************
* @brief                    Verify signature of processed data
* @param [in] pCtx          Signature context
* @param [in] pKey          RSA private key
* @param [in] pSignature    Signature
* @param [in] length        Signature length in bytes
* @param [in] duidHash      DUID hash
* @return                   Signature verification result
****************************************************************************************************
*/
SF_STATUS SignatureVerify( UepSignatureContext* pCtx, const UepKey* pKey, const Uint8* pSignature,
                           Uint32 length, const Uint8* duidHash );

/**
****************************************************************************************************
* @brief                    Free inner resources of signature context
* @param [in] pCtx          Signature context
* @return
****************************************************************************************************
*/
void FreeSignatureContext( UepSignatureContext* pCtx );

#endif  /* _UEP_SIGNATURE_CONTEXT_H_ */