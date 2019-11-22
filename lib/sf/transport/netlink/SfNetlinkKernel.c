/**
****************************************************************************************************
* @file SfNetlinkUser.c
* @brief Security framework [SF] filter driver [D] Implementation of the Netlink transport
*   mechanisms for kernel space modules
* @author Dmitriy Dorogovtsev(d.dorogovtse@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Anton Skakun (a.skakun@samsung.com)
* @date Created May 23, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfMemory.h>

#include <uapi/linux/sf/transport/SfTransport.h>
#include <uapi/linux/sf/transport/SfSerialization.h>
#include <uapi/linux/sf/protocol/SfPacket.h>

//#include "dispatcher/SfRulesList.h"

#include <net/sock.h>

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfSendPacket( SfNode* const pNode, const SfPacket* const pPacket )
{
    SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;
    SfNode* pCurrentNode = pNode;

    if ( !pPacket || !pPacket->env )
    {
        SF_LOG_E( "%s(): packet or its environment is NULL", __FUNCTION__ );
        return SF_STATUS_BAD_ARG;
    }

    do
    {
        SfNetlinkPacket* pNetlinkPacket = NULL;

        // check if nobody wants to receive this packet
        // for some reason it does not work on Tizen kernel
        if ( !netlink_has_listeners( pCurrentNode->pHandle, pPacket->op->type ) )
        {
            result = SF_STATUS_OK;
            break;
        }

        pNetlinkPacket = SfSerializePacket( pPacket );
        if ( !pNetlinkPacket )
        {
            SF_LOG_E( "%s(): failed to serialize packet", __FUNCTION__ );
            result = SF_STATUS_FAIL;
            break;
        }

        // broadcast message
        nlmsg_multicast( pCurrentNode->pHandle, pNetlinkPacket->pBuffer, 0, pPacket->op->type,
                         GFP_KERNEL );
        // fix 4byte memory leak, free just SfNetlinkPacket not sk_buff. refer DF141103-00255.
        sf_free( pNetlinkPacket );
	pNetlinkPacket = NULL;

        result = SF_STATUS_OK;
    }
    while ( FALSE );
    return result;
}
EXPORT_SYMBOL(SfSendPacket);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfReceivePacket( SfNode* const pNode, SfPacket** const ppPacket )
{
    SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;

    return result;
}

void SfBind(int group)
{
    SF_LOG_I("Bind was called for process %s and pid %d", current->comm, current->pid);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfCreateNode(SfNode** ppNode, const Char* const name, Ulong id,
    SfReceiveMessagePtr pHandler)
{
    SF_STATUS result = SF_STATUS_OK;
    SfNode* pNode = NULL;
    struct netlink_kernel_cfg netlinkConfig =
    {
        .bind = SfBind,
        .input = NULL,
        .flags = NL_CFG_F_NONROOT_RECV
    };

    do
    {
        if (NULL == ppNode || NULL == name)
        {
            SF_LOG_E("%s ppNode parameter equals to %p", __FUNCTION__, ppNode);
            result = SF_STATUS_BAD_ARG;
            break;
        }

        *ppNode = sf_malloc(sizeof(SfNode));
        if (NULL == *ppNode)
        {
            SF_LOG_E("%s *ppNode equals to %p", __FUNCTION__, *ppNode);
            result = SF_STATUS_FAIL;
            break;
        }

        pNode = *ppNode;
        netlinkConfig.input = pHandler;

        sf_strncpy(pNode->id.name, name, sizeof(pNode->id.name));
        pNode->id.magic = id;

        pNode->pHandle = netlink_kernel_create(&init_net, SF_PROTOCOL_NUMBER, &netlinkConfig);

        if (NULL == pNode->pHandle)
        {
            SF_LOG_E("%s Can not create netlink socket", __FUNCTION__);
            result = SF_STATUS_FAIL;
            sf_free(*ppNode);
            *ppNode = NULL;
        }

    } while(FALSE);

    return result;
}
EXPORT_SYMBOL(SfCreateNode);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfDestroyNode(SfNode* const pNode)
{
    SF_STATUS result = SF_STATUS_FAIL;

    if (NULL != pNode)
    {
        if (NULL != pNode->pHandle)
        {
            netlink_kernel_release(pNode->pHandle);
        }
        sf_free(pNode);
        result = SF_STATUS_OK;
    }

    return result;
}
EXPORT_SYMBOL(SfDestroyNode);
