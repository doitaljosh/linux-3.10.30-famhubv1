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
 * @file		ess_dlist.c
 * @brief		Double liked list
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

// include for ESS 
#include "ess_types.h"
#include "ess_dlist.h"

// debug begin
#include "ess_debug.h"
// debug end


/** 
 * @brief remove a node from a list
 * @param	pNode : list pNode to be deleted
 * @return	void
*/
void
EssDList_Del(EssDList* pNode)
{

	ESS_ASSERT(pNode != NULL);

	ESS_DLIST_DEL(pNode->pPrev, pNode->pNext);
	pNode->pPrev = NULL;
	pNode->pNext = NULL;
}


/** 
 * @brief	inverse a list
 * @param	pHead : list head
 * @return	inversed list
*/
void
ESS_DLIST_INVERSE(EssDList* pHead)
{
	EssDList*	tmp;
	EssDList*	tmp1;

	ESS_ASSERT(pHead);
	ESS_ASSERT(ESS_DLIST_IS_EMPTY(pHead) != 1);

	//// inverse pNodes
	ESS_DLIST_FOR_EACH_SAFE(tmp, tmp1, pHead)
	{
		tmp1 = tmp->pNext;
		tmp->pNext = tmp->pPrev;
		tmp->pPrev = tmp1;
	}

	//// inverse pHead
	tmp = pHead->pNext;
	pHead->pNext = pHead->pPrev;
	pHead->pPrev = tmp;
}


/** 
 * @brief	This function count entry count
 * @param	pHead : list pHead to be checked
 * @return  	number of nodes
*/
t_int32
EssDList_Count(EssDList* pHead)
{
	t_int32		dwCount;
	EssDList*	pNext;

	ESS_ASSERT(pHead);

	dwCount = 0;
	pNext = pHead->pNext;
	while (pNext != pHead)
	{
		dwCount++;
		pNext = pNext->pNext;

		ESS_ASSERT(pNext->pPrev->pNext == pNext);
		ESS_ASSERT(pNext == pNext->pNext->pPrev);
	}

	return dwCount;
}


/** 
 * @brief	Check the list is empty
 * @param	pHead : list pHead to be checked
 * @return	true : it is empty
 * @return	false : it is not empty
*/
t_int32
EssDList_IsEmptyCareFul(EssDList* pHead)
{
	EssDList*	pNext = pHead->pNext;
	return (pNext == pHead) && (pNext == pHead->pPrev);
}


#ifdef ESS_DLIST_DEBUG
	void
	ESS_DLIST_INIT(EssDList* p)
	{
		(p)->pPrev = (p); (p)->pNext = (p);
	}

	void
	ESS_DLIST_ADD(EssDList* p, EssDList* n, EssDList* w)
	{
		(w)->pPrev = (p); (w)->pNext = (n); (n)->pPrev = (w); (p)->pNext = (w);
	}

	void
	ESS_DLIST_ADD_HEAD(EssDList* h, EssDList* n)
	{
		EssDList* pTmp = (h)->pNext; ESS_DLIST_ADD((h), pTmp, (n));
	}

	void
	ESS_DLIST_ADD_TAIL(EssDList* h, EssDList* n)
	{
		EssDList* pTmp = (h)->pPrev; ESS_DLIST_ADD(pTmp, (h), (n));
	}

	void
	ESS_DLIST_DEL(EssDList* p, EssDList* n)
	{
		(p)->pNext = (n); (n)->pPrev = (p);
	}

	void
	ESS_DLIST_DEL_INIT(EssDList* p)
	{
		ESS_DLIST_DEL((p)->pPrev, (p)->pNext); ESS_DLIST_INIT((p));
	}

	void
	ESS_DLIST_MOVE_HEAD(EssDList* h, EssDList* n)
	{
		ESS_DLIST_DEL((n)->pPrev, (n)->pNext); ESS_DLIST_ADD_HEAD((h), (n));
	}

	void
	ESS_DLIST_MOVE_TAIL(EssDList* h, EssDList* n)
	{
		ESS_DLIST_DEL((n)->pPrev, (n)->pNext); ESS_DLIST_ADD_TAIL((h), (n));
	}

	t_boolean
	ESS_DLIST_IS_EMPTY(EssDList* h)
	{
		return (h)->pNext == (h) ? ESS_TRUE : ESS_FALSE;
	}

	EssDList*
	ESS_DLIST_GET_NEXT(EssDList* p)
	{
		return ((p)->pNext);		// get next entry
	}

	EssDList*
	ESS_DLIST_GET_PREV(EssDList* p)
	{
		return ((p)->pPrev);		// get previous entry
	}

	EssDList*
	ESS_DLIST_GET_TAIL(EssDList* h)
	{
		return ((h)->pPrev);		// get tail entry
	}

	//!< move a list to head of another list
	//!< pHeadFrom : head of list from.
	//!< pHeadTo : head of list to
	void
	ESS_DLIST_MERGE_LIST(EssDList* pHeadFrom, EssDList* pHeadTo)
	{
		(pHeadFrom)->pNext->pPrev	= (pHeadTo);
		(pHeadFrom)->pPrev->pNext	= (pHeadTo)->pNext;
		(pHeadTo)->pNext->pPrev		= (pHeadFrom)->pPrev;
		(pHeadTo)->pNext			= (pHeadFrom)->pNext;
		ESS_DLIST_INIT(pHeadFrom);
	}
#endif	// end of #ifdef ESS_DLIST_DEBUG

