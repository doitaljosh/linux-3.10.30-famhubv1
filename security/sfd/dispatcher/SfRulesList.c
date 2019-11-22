/**
****************************************************************************************************
* @file SfRulesList.c
* @brief Security framework [SF] filter driver [D] blocking rules list
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Sep 24, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include "SfRulesList.h"

#include <linux/types.h>
#include <asm/unistd.h>
#include <linux/list.h>
#include <linux/rwsem.h>

typedef struct
{
    struct list_head node;
    Uint32           ipAddr;
} NetworkRule;

typedef struct
{
    struct list_head node;
    Uint64           fileInode;
} FileRule;

static LIST_HEAD(s_netRulesList);
static LIST_HEAD(s_fileRulesList);

static DECLARE_RWSEM(s_netSem);
static DECLARE_RWSEM(s_fileSem);

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS AddNetworkRule( Uint32 ipAddr )
{
    SF_STATUS r = SF_STATUS_FAIL;
    NetworkRule* pRule = sf_malloc( sizeof(NetworkRule) );
    if ( pRule )
    {
        pRule->ipAddr = ipAddr;

        // append new rule to list under write lock
        down_write( &s_netSem );
        list_add_tail( &pRule->node, &s_netRulesList );
        up_write( &s_netSem );

        SF_LOG_I( "%s(): added network block rule for %pI4", __FUNCTION__, &ipAddr );
        r = SF_STATUS_OK;
    }
    else
    {
        SF_LOG_E( "%s(): failed to allocate network rule", __FUNCTION__ );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS AddFileRule( Uint64 inode )
{
    SF_STATUS r = SF_STATUS_FAIL;
    FileRule* pRule = sf_malloc( sizeof(FileRule) );
    if ( pRule )
    {
        pRule->fileInode = inode;

        // append new rule to list under write lock
        down_write( &s_fileSem );
        list_add_tail( &pRule->node, &s_fileRulesList );
        up_write( &s_fileSem );

        SF_LOG_I( "%s(): added file block rule for %lu", __FUNCTION__, inode );
        r = SF_STATUS_OK;
    }
    else
    {
        SF_LOG_E( "%s(): failed to allocate file rule", __FUNCTION__ );
    }
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
Bool NetworkAccessRestricted( Uint32 ipAddr )
{
    Bool r = FALSE;
    NetworkRule* pRule = NULL;

    // check if address is blocked under read lock
    down_read( &s_netSem );
    list_for_each_entry( pRule, &s_netRulesList, node )
    {
        if ( ipAddr == pRule->ipAddr )
        {
            r = TRUE;
            break;
        }
    }
    up_read( &s_netSem );
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
Bool FileAccessRestricted( Uint64 inode )
{
    Bool r = FALSE;
    FileRule* pRule = NULL;

    // check if file is blocked under read lock
    down_read( &s_fileSem );
    list_for_each_entry( pRule, &s_fileRulesList, node )
    {
        if ( inode == pRule->fileInode )
        {
            r = TRUE;
            break;
        }
    }
    up_read( &s_fileSem );
    return r;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void ClearNetworkRulesList( void )
{
    // no locking here
    NetworkRule *p = NULL, *n = NULL;
    list_for_each_entry_safe( p, n, &s_netRulesList, node )
    {
        list_del( &p->node );
        sf_free( p );
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void ClearFileRulesList( void )
{
    // no locking here
    FileRule *p = NULL, *n = NULL;
    list_for_each_entry_safe( p, n, &s_fileRulesList, node )
    {
        list_del( &p->node );
        sf_free( p );
    }
}