
#include <uapi/linux/sf/transport/netlink/SfNetlinkSerialization.h>
#include <uapi/linux/sf/core/SfDebug.h>

#if defined(SF_LEVEL_USER)
    #include "libnl/include/netlink/netlink.h"
    #include "libnl/include/netlink/genl/genl.h"
    #include "libnl/include/netlink/genl/ctrl.h"
    #include "libnl/include/netlink/genl/mngt.h"
#else
    #include <net/sock.h>
#endif  /* !SF_LEVEL_USER */

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SfNetlinkPacket* SfCreateNetlinkPacket(void)
{
    SfNetlinkPacket* pPacket = sf_malloc( sizeof(SfNetlinkPacket) );
    if ( NULL != pPacket )
    {
#if defined(SF_LEVEL_USER)
        pPacket->pBuffer = nlmsg_alloc();
#else
        pPacket->pBuffer = nlmsg_new( NLMSG_DEFAULT_SIZE, GFP_ATOMIC );
#endif  /* !SF_LEVEL_USER */

        if ( NULL == pPacket->pBuffer )
        {
            SF_LOG_E( "%s(): nlmsg_new() failed to allocate netlink message", __FUNCTION__ );
            sf_free( pPacket );
            pPacket = NULL;
        }
    }
    else
    {
        SF_LOG_E( "%s(): failed to allocate SfNetlinkPacket", __FUNCTION__ );
    }
    return pPacket;
}
#if defined(SF_LEVEL_KERNEL)
EXPORT_SYMBOL(SfCreateNetlinkPacket);
#endif  // SF_LEVEL_KERNEL

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyNetlinkPacket(SfNetlinkPacket* const pPacket)
{
    if ( pPacket )
    {
        nlmsg_free( pPacket->pBuffer );
        sf_free( pPacket );
    }
}
#if defined(SF_LEVEL_KERNEL)
EXPORT_SYMBOL(SfDestroyNetlinkPacket);
#endif  // SF_LEVEL_KERNEL