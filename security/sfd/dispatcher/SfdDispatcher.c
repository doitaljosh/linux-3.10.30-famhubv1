/**
****************************************************************************************************
* @file SfdDispatcher.c
* @brief Security framework [SF] filter driver [D] modules dispather implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdDispatcher.h"
#include "SfdPlatformInterface.h"
#include "SfRulesList.h"

#include "uep/SfdUepHookHandlers.h"

#include <linux/in.h>
#include <linux/fs.h>

#include <linux/rculist.h>

#include <uapi/linux/sf/transport/SfSerialization.h>
#include <uapi/linux/sf/transport/SfNetlink.h>
#include <uapi/linux/sf/transport/SfProtocolHeaderSerialization.h>
#include <uapi/linux/sf/transport/SfPacketEnvironmentSerialization.h>
#include <uapi/linux/sf/transport/SfPacketOperationSerialization.h>

/**
* @brief Global dispatcher pointer definition. This pointer may be used as global symbol by other
*   kernel modules.
*/
SfdDispatcherContext* g_pDispatcher = NULL;


/**
****************************************************************************************************
* @brief                    Handle rule update request
* @param [in] pRule         Rule
* @return                   void
****************************************************************************************************
 */
static void HandleRuleUpdate( const SfOperationBlockRule* pRule )
{
    if ( ( pRule->ruleType == SF_RULE_SOCKET_CONNECT ) && ( pRule->action == SF_RULE_ADD ) )
    {
        AddNetworkRule( pRule->ipAddr );
    }
    else if ( ( pRule->ruleType == SF_RULE_FILE_OPEN ) && ( pRule->action == SF_RULE_ADD ) )
    {
        AddFileRule( pRule->fileInode );
    }
}

/**
****************************************************************************************************
* @brief                    Callback for receiving messages
* @param [in] skb           Input socket buffer
* @return                   void
****************************************************************************************************
 */
static void SfReceiveMessageCallback( struct sk_buff* skb )
{
    SfNetlinkPacket netlinkPacket = { .pBuffer = skb };
    SfPacket* pPacket = SfDeserializePacket( &netlinkPacket );

    if ( pPacket )
    {
        SF_LOG_I( "%s(): received packet, type = %d", __FUNCTION__, pPacket->header.type );
        if ( pPacket->op )
        {
            if ( pPacket->op->type == SF_OPERATION_TYPE_RULE )
            {
                SfOperationBlockRule* pRule = (SfOperationBlockRule*)( pPacket->op );
                SF_LOG_I( "%s(): received rule, type %u, action %u, addr %u, inode %lu",
                          __FUNCTION__, pRule->ruleType, pRule->action, pRule->ipAddr,
                          pRule->fileInode );
                HandleRuleUpdate( pRule );
            }
            else if ( pPacket->op->type == SF_OPERATION_TYPE_SETUP_DUID )
            {
                SfOperationSetupDUID* pDuidOp = (SfOperationSetupDUID*)( pPacket->op );
                SF_LOG_I( "%s(): received DUID = [%s]", __FUNCTION__, pDuidOp->pDUID );
                SetupDuidHash( pDuidOp->pDUID );
            }
            else
                SF_LOG_W( "%s(): unknown operation type", __FUNCTION__ );
        }
        SfDestroyPacket( pPacket );
    }
    else
    {
        SF_LOG_E( "%s(): failed to deserialize packet", __FUNCTION__ );
    }
}


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenDispatcherContext(SfdDispatcherContext* const pDispatcher)
{
    SF_STATUS result = SF_STATUS_FAIL;

    /**
    * @note If you want to use this macros in your code, please, check it's implementation before.
    *   Implementation located in libcore/SfValidator.h
    */
    result = SF_CONTEXT_SAFE_INITIALIZATION(pDispatcher, SfdDispatcherContext,
        SF_CORE_VERSION, SfdCloseDispatcherContext);

    if (SF_SUCCESS(result))
    {
        g_pDispatcher = pDispatcher;

        pDispatcher->module.moduleType = SFD_MODULE_TYPE_DISPATCHER;
        INIT_LIST_HEAD(&pDispatcher->module.list);

        result = SfdCreatePlatformInterface();

        if (SF_SUCCESS(result))
        {
            result = SfCreateNode(&pDispatcher->module.pNode, "Disp", 0xbedabeda, SfReceiveMessageCallback);
            if (SF_SUCCESS(result))
            {
                pDispatcher->header.state = SF_CONTEXT_STATE_INITIALIZED;
            }
        }

        SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
    }

    return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseDispatcherContext(SfdDispatcherContext* const pDispatcher)
{
    SF_STATUS result = SF_STATUS_FAIL;

    if (!SfIsContextValid(&pDispatcher->header, sizeof(SfdDispatcherContext)))
    {
        SF_LOG_E("Invalid 'pDispatcher'");
        return SF_STATUS_BAD_ARG;
    }

    result = SfdDestroyPlatformInterface();

    if (SF_SUCCESS(result))
    {
        pDispatcher->header.size = 0;
        pDispatcher->header.state = SF_CONTEXT_STATE_UNINITIALIZED;
    }

    result = SfDestroyNode(pDispatcher->module.pNode);
    g_pDispatcher = NULL;
    ClearNetworkRulesList();
    ClearFileRulesList();

    SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
    return result;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
static SF_STATUS SfdPerformBlocking(SfProtocolHeader* const pOperation)
{
    SF_STATUS result = SF_STATUS_OK;
    do
    {
        if (NULL == pOperation)
        {
            break;
        }

        switch(pOperation->type)
        {
            case SF_OPERATION_TYPE_OPEN:
            {
                SfOperationFileOpen* pFileOpenOperation = (SfOperationFileOpen*) pOperation;
                if (NULL != pFileOpenOperation->pFile)
                {
                    if (FileAccessRestricted(pFileOpenOperation->pFile->f_dentry->d_inode->i_ino))
                    {
                        SF_LOG_I( "%s(): access to file with inode %lu restricted", __FUNCTION__,
                            pFileOpenOperation->pFile->f_dentry->d_inode->i_ino );
                        result = SF_STATUS_RESOURCE_BLOCK;
                        pFileOpenOperation->result = result;
                    }
                }
            }
            break;

            case SF_OPERATION_TYPE_CONNECT:
            {
                SfOperationSocketConnect* pSocketOperation = (SfOperationSocketConnect*) pOperation;
                if (NULL != pSocketOperation->pAddress &&
                    AF_INET == pSocketOperation->pAddress->sa_family)
                {
                    struct sockaddr_in* pInAddr = (struct sockaddr_in*) pSocketOperation->pAddress;
                    __be32 ipAddr = pInAddr->sin_addr.s_addr;
                    if ( NetworkAccessRestricted( ipAddr ) )
                    {
                        SF_LOG_I( "%s(): access to %pI4 restricted", __FUNCTION__, &ipAddr );
                        result = SF_STATUS_RESOURCE_BLOCK;
                        pSocketOperation->result = result;
                    }
                }
            }
            break;

            default:
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
SF_STATUS SfdProcessOperationThroughModules(SfProtocolHeader* const pOperation)
{
    SF_STATUS result = SF_STATUS_OK;
    SfdModuleInterface* pModule = NULL;
    SfPacket packet =
    {
        .header =
        {
            .size = sizeof(SfPacket),
            .type = SF_PACKET_TYPE_OPERATION
        },
        .env = NULL,
        .op = pOperation
    };

    do
    {
        if (NULL == g_pDispatcher)
        {
            break;
        }

        rcu_read_lock();
        {
            list_for_each_entry_rcu(pModule, &g_pDispatcher->module.list, list)
            {
                if (NULL != pModule &&
                    NULL != pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE])
                {
                    /**
                    * @note Passing header in this case will pass the C compiler rules. Header,
                    *   is every time first bytes in the protol structure. Header is necessary
                    *   to verify passed data. The same as for the following cycle.
                    */
                    result =
                        pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE](&packet.header);
                    if (SF_FAILED(result))
                    {
                        break;
                    }
                }
            }

            if (pOperation->type == SF_OPERATION_TYPE_OPEN ||
                pOperation->type == SF_OPERATION_TYPE_CONNECT)
            {
                result = SfdPerformBlocking(pOperation);
            }

            list_for_each_entry_rcu(pModule, &g_pDispatcher->module.list, list)
            {
                if (NULL != pModule &&
                    NULL != pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION])
                {
                    pModule->PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION](&packet.header);
                }
            }
        }
        rcu_read_unlock();

        if (NULL != packet.env)
        {
           SfDestroyEnvironment(packet.env);
        }
    } while(FALSE);

    return result;
}
