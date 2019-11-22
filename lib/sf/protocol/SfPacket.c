/**
****************************************************************************************************
* @file SfPacket.c
* @brief Security framework [SF] filter driver [D] SfPacket support utilities
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Sep 15, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/protocol/SfPacket.h>

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfDestroyPacket( SfPacket* pPacket )
{
    if ( pPacket )
    {
        if ( pPacket->env )
        {
            SfDestroyEnvironment( pPacket->env );
        }
        if ( pPacket->op )
        {
            SfDestroyOperation( pPacket->op );
        }
        sf_free( pPacket );
    }
}