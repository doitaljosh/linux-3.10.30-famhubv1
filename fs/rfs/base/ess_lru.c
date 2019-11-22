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
 * @file		ess_lru.h
 * @brief		Reast Lecently Used Algorithm.
 * @brief		EssLru does not re-entrant one.
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-11-2006 [DongYoung Seo] First writing
 * @see			None
 */

// headers
#include "ess_lru.h"
#include "ess_debug.h"


/**
 * Initialize EssLru structure
 * new node will be structure at the first of free list
 *
 * @param		pLruHead		: LRU structure pointer
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
t_int32
EssLru_Init(EssLru* pLruHead)
{
	if (pLruHead == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}

	ESS_ASSERT(pLruHead);

	ESS_DLIST_INIT(&pLruHead->stDListLru);

	return ESS_OK;
}


/**
 * lookup a node from EssLru
 * new node will be attached at the first of free list
 *
 * @param		pLruHead		: LRU pointer
 * @param		pTarget		: comparison data storage (user purpose)
 * @param		pfCmp		: comparison function pointer
								This function receives two parameter,
								param1 : input parameter pTarget
								param2 : an entry at LRU list.
 * @return		NULL		: lookup fail
 * @return		else		: node pointer
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
EssDList*
EssLru_Lookup(EssLru* pLruHead, void* pTarget, PFN_LRU_CMP pfCmp)
{

	EssDList*		pos;	// current position

#ifdef ESS_LRU_STRICT_CHECK
	if ((pLruHead == NULL) || (pTarget == NULL) || (pfCmp == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		ESS_ASSERT(0);
		return NULL;
	}
#endif

	ESS_ASSERT(pLruHead);
	ESS_ASSERT(pTarget);
	ESS_ASSERT(pfCmp);

	ESS_DLIST_FOR_EACH(pos, &pLruHead->stDListLru)
	{
		if (pfCmp(pTarget, (EssLruEntry*)pos))
		{
			ESS_DLIST_MOVE_HEAD(&(pLruHead->stDListLru), pos);
			return pos;
		}
	}

	return NULL;
}


/**
 * Remove a node from LRU
 * The entry should be at free list
 *
 * @param		pEntry		: entry pointer.that will be removed
 * @return		void
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 */
void
EssLru_RemoveInit(EssLruEntry* pEntry)
{
#ifdef ESS_LRU_STRICT_CHECK
	if (pEntry == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pEntry);

	_ESS_LRU_REMOVE_INIT(pEntry);
}


/**
 * Get a recent accessed node.
 *
 * @param		pLruHead		: LRU structure pointer
 * @return		first node pointer
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
EssDList*
EssLru_GetHead(EssLru* pLruHead)
{

	EssDList*		pHead;

#ifdef ESS_LRU_STRICT_CHECK
	if (pLruHead == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return NULL;
	}
#endif

	ESS_ASSERT(pLruHead);

	if (ESS_DLIST_IS_EMPTY(&pLruHead->stDListLru))
	{
		return NULL;
	}

	pHead = pLruHead->stDListLru.pNext;

	return pHead;

}


/**
 * Get a least recent accessed node.
 *
 * @param		pLruHead		: LRU structure pointer
 * @return		last node pointer
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
EssLruEntry*
EssLru_GetTail(EssLru* pLruHead)
{
	EssDList*	pTail;

#ifdef ESS_LRU_STRICT_CHECK
	if (pLruHead == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		ESS_ASSERT(0);
		return NULL;
	}
#endif

	ESS_ASSERT(pLruHead);

	if (ESS_DLIST_IS_EMPTY(&pLruHead->stDListLru))
	{
		return NULL;
	}

	pTail = pLruHead->stDListLru.pPrev;

	return ESS_GET_ENTRY(pTail, EssLruEntry, stDListLru);
}


/**
 * Move a node to head of LRU list
 *
 * @param		pLruHead		: LRU structure pointer
 * @param		pEntry		: new free node pointer.
 * @author		DongYoung Seo
 * @version		JUL-11-2006 [DongYoung Seo] First Writing.
 */
void
EssLru_MoveToHead(EssLru* pLruHead, EssLruEntry* pEntry)
{

#ifdef ESS_LRU_STRICT_CHECK
	if ((pLruHead == NULL) || (pEntry == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		ESS_ASSERT(0);
		return;
	}
#endif

	ESS_ASSERT(pLruHead);
	ESS_ASSERT(pEntry);

	_ESS_LRU_MOVE_TO_HEAD(pLruHead, pEntry);
}


