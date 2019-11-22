
#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfMemory.h>

#include "uep/UepKey.h"

/*
****************************************************************************************************
*
****************************************************************************************************
*/
UepKey* CreateKey( const Char* pubRsaN, const Char* pubRsaE )
{
    UepKey* pKey = sf_malloc( sizeof(UepKey) );
    if ( !pKey )
        return NULL;

    sf_memset( pKey, 0, sizeof(UepKey) );
    pKey->rsaN = mpi_alloc( 0 );
    pKey->rsaE = mpi_alloc( 0 );
    if ( !pKey->rsaN || !pKey->rsaE )
    {
        DestroyKey( pKey );
        return NULL;
    }

    if ( mpi_fromstr( pKey->rsaN, pubRsaN ) || mpi_fromstr( pKey->rsaE, pubRsaE ) )
    {
        SF_LOG_E( "%s(): failed to create RSA key from strings" );
        DestroyKey( pKey );
        return NULL;
    }
    return pKey;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void DestroyKey( UepKey* pKey )
{
    if ( pKey )
    {
        if ( pKey->rsaN )
            mpi_free( pKey->rsaN );
        if ( pKey->rsaE )
            mpi_free( pKey->rsaE );
        sf_free( pKey );
    }
}