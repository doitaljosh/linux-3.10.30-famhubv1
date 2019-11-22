/**
 *     @mainpage   Nestle Layer
 *
 *     @section Intro
 *       TFS5 File system's VFS framework
 *   
 *     @MULTI_BEGIN@ @COPYRIGHT_DEFAULT
 *     @section Copyright COPYRIGHT_DEFAULT
 *            COPYRIGHT. SAMSUNG ELECTRONICS CO., LTD.
 *                                    ALL RIGHTS RESERVED
 *     Permission is hereby granted to licensees of Samsung Electronics Co., Ltd. products
 *     to use this computer program only in accordance 
 *     with the terms of the SAMSUNG FLASH MEMORY DRIVER SOFTWARE LICENSE AGREEMENT.
 *     @MULTI_END@
 *
 */

/** 
 * @file		ess_hash.c
 * @brief		Hash module
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-11-2006 [DongYoung Seo] First writing
 * @see			None
 */


#include "ess_hash.h"

// debug begin
#include "ess_debug.h"
// debug end

/**
 * Initialize EssHash structure
 * All hash bucket is double linked list (EssDList)
 *
 * @param		pHash			: hash structure pointer
 * @param		pHashTable		: the first entry pointer of bucket
 * @param		dwBucketCount	: bucket count
 * @return		ESS_OK			: success
 * @return		ESS_EINVALID	: Invalid parameter
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
t_int32
EssHash_Init(EssHash* pHash, EssDList* pHashTable, t_int32 dwBucketCount)
{
	t_int32		i;

	if ((pHash == NULL) || (pHashTable == NULL) || (dwBucketCount <= 0))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		return ESS_EINVALID;
	}

	ESS_ASSERT(pHash);
	ESS_ASSERT(pHashTable);
	ESS_ASSERT(dwBucketCount > 0);

	pHash->pHashTable	= pHashTable;
	pHash->dwBucketMax	= dwBucketCount - 1;

	for (i = 0; i < dwBucketCount; i++)
	{
		ESS_DLIST_INIT(&pHash->pHashTable[i]);
	}

	ESS_DLIST_INIT(&pHash->stDListFree);

	return ESS_OK;
}

/**
 * Add a node to hash table
 * New node will be positioned at the head of bucket
 *
 * @param		pHash		: hash structure pointer
 * @param		dwHashVal	: hash value, index of bucket
 * @param		pEntry		: node pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssHash_Add(EssHash* pHash, t_int32 dwHashVal, EssHashEntry* pEntry)
{

#ifdef ESS_HASH_STRICT_CHECK
	if ((pHash == NULL) || (pEntry == NULL) || (dwHashVal < 0) || (dwHashVal > pHash->dwBucketMax))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pHash);
	ESS_ASSERT(pEntry);
	ESS_ASSERT(dwHashVal >= 0);
	ESS_ASSERT(dwHashVal <= pHash->dwBucketMax);

	_ESS_HASH_ADD(pHash, dwHashVal, pEntry);
}

/**
 * remove a node from hash table
 *
 * @param		pEntry		: node pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssHash_Remove(EssHashEntry* pEntry)
{

#ifdef ESS_HASH_STRICT_CHECK
	if (pEntry == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pEntry);

	_ESS_HASH_REMOVE(pEntry);
}


/**
 * move a node to head of the bucket
 *
 * @param		pHash		: hash structure pointer
 * @param		dwHashVal	: hash value, index of bucket
 * @param		pEntry		: node pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssHash_MoveToHead(EssHash* pHash, t_int32 dwHashVal, EssHashEntry* pEntry)
{

#ifdef ESS_HASH_STRICT_CHECK
	if ((pHash == NULL) || (pEntry == NULL) || (dwHashVal < 0) || (dwHashVal > pHash->dwBucketMax))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pHash);
	ESS_ASSERT(pEntry);
	ESS_ASSERT(dwHashVal >= 0);
	ESS_ASSERT(dwHashVal <= pHash->dwBucketMax);

	_ESS_HASH_MOVE_HEAD(pHash, dwHashVal, pEntry);
}

/**
 * remove a node from hash table
 *
 * @param		pHash		: hash structure pointer
 * @param		dwHashVal	: hash value, index of bucket
 * @param		pTarget		: target pointer
 * @param		pfCmp		: compare function pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
EssHashEntry*
EssHash_Lookup(EssHash* pHash, t_int32 dwHashVal, void* pTarget, PFN_HASH_CMP pfCmp)
{
	EssDList*		pos;
	EssDList*		pHead;
	EssHashEntry*	pEntry;

#ifdef ESS_HASH_STRICT_CHECK
	if ((pHash == NULL) || (pTarget == 0) || (pfCmp == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return NULL;
	}
#endif

	ESS_ASSERT(pHash);
	ESS_ASSERT(dwHashVal >= 0);
	ESS_ASSERT(dwHashVal <= pHash->dwBucketMax);
	ESS_ASSERT(pTarget);
	ESS_ASSERT(pfCmp);

	pHead = &pHash->pHashTable[dwHashVal];

	ESS_DLIST_FOR_EACH(pos, pHead)
	{
		pEntry = ESS_GET_ENTRY(pos, EssHashEntry, stDListHash);
		if (pfCmp(pTarget, pEntry))
		{
			ESS_DLIST_MOVE_HEAD(pHead, &pEntry->stDListHash);
			return pEntry;
		}
	}

	return NULL;
}


/**
 * add a node to free list
 *
 * @param		pHash		: hash structure pointer
 * @param		pEntry		: a free node
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssHash_AddToFree(EssHash* pHash, EssHashEntry* pEntry)
{

#ifdef ESS_HASH_STRICT_CHECK
	if ((pHash == NULL) || (pEntry == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pHash);
	ESS_ASSERT(pEntry);

	_ESS_HASH_ADD_TO_FREE(pHash, pEntry);
}

/**
 * remove a node from hash table
 *
 * @param		pHash		: hash structure pointer
 * @param		pEntry		: node pointer to be moved free list
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssHash_MoveToFree(EssHash* pHash, EssHashEntry* pEntry)
{

#ifdef ESS_HASH_STRICT_CHECK
	if ((pHash == NULL) || (pEntry == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return;
	}

	if (ESS_DLIST_IS_EMPTY(&pEntry->stDListHash))
	{
		ESS_LOG_PRINTF("pEntry is an emtry list");
		// Programming error
		// to add a node to free list use EssHas_AddToFree()
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pHash);
	ESS_ASSERT(pEntry);

	_ESS_HASH_MOVE_TO_FREE(pHash, pEntry);
}


/**
* get a free node from free list
*
* It does not remove entry from free list
*
* @param		pHash		: hash structure pointer
* @return		valid ptr	: an node pointer
* @return		NULL		: there is no free node
* @author		DongYoung Seo
* @version		JUL-11-2006 [DongYoung Seo] First Writing.
*/
EssHashEntry*
EssHash_GetFree(EssHash* pHash)
{

	EssHashEntry*	pEntry;

#ifdef ESS_HASH_STRICT_CHECK
	if (pHash == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter");
		ESS_ASSERT(0);
		return NULL;
	}
#endif

	ESS_ASSERT(pHash);

	if (ESS_DLIST_IS_EMPTY(&pHash->stDListFree) == ESS_TRUE)
	{
		// there is no free entry
		return NULL;
	}

	pEntry = ESS_GET_ENTRY(ESS_DLIST_GET_TAIL(&pHash->stDListFree), EssHashEntry, stDListHash);
	ESS_ASSERT(pEntry);

	return pEntry;
}



