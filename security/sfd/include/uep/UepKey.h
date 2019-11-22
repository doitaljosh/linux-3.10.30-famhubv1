#ifndef _UEP_KEY_H_
#define _UEP_KEY_H_

#include <uapi/linux/sf/core/SfTypes.h>
#include <linux/mpi.h>

typedef struct
{
    MPI rsaN;
    MPI rsaE;
} UepKey;

UepKey* CreateKey( const Char* pubRsaN, const Char* pubRsaE );

void DestroyKey( UepKey* pKey );

#endif  // _UEP_KEY_H_