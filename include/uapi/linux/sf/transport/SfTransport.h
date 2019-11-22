/**
****************************************************************************************************
* @file SfdNetlinkSetup.h
* @brief Security framework [SF] transport system
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created May 26, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_TRANSPORT_H_
#define _SF_TRANSPORT_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#if defined(SF_TRANSPORT_NETLINK)
#include "SfNetlink.h"
#else
#error Please, specify which transport mechanisms must be used for communication
#endif  /* !SF_TRANSPORT_NETLINK */

//#include "protocol/SfPacket.h"
#include <uapi/linux/sf/protocol/SfPacket.h>

struct sock;

#define SF_NODE_NAME_LENGTH 8

typedef struct
{
    Char name[SF_NODE_NAME_LENGTH];
    Ulong magic;
} SfNodeId;

struct sockaddr_nl;

struct sk_buff;

typedef void (*SfReceiveMessagePtr)( struct sk_buff* skb );

typedef struct
{
#if defined(SF_TRANSPORT_NETLINK)
#if defined(SF_LEVEL_KERNEL)
    struct sock* pHandle; ///< Socket to create netlink communication
#elif defined(SF_LEVEL_USER)
    struct nl_sock* pHandle; ///< Socket to create betlink communication
#endif /* !SF_LEVEL_KERNEL */
#endif  /* !SF_TRANSPORT_NETLINK */

    SfNodeId id;

} SfNode;

SF_STATUS SFAPI SfCreateNode(SfNode** ppNode, const Char* const name, Ulong id,
	SfReceiveMessagePtr pHandler);

SF_STATUS SFAPI SfDestroyNode(SfNode* const pNode);

SF_STATUS SFAPI SfSendPacket(SfNode* const pNode, const SfPacket* const pPacket);


SF_STATUS SFAPI SfReceivePacket(SfNode* const pNode, SfPacket** const ppPacket);

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif  /* !_SF_TRANSPORT_H_ */