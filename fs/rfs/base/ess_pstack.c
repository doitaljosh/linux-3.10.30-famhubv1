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
 * @file		ess_pstack.c
 * @brief		Static memory allocator with stack scheme.
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_pstack.h"
#include "ess_dlist.h"
#include "ess_math.h"
#include "ess_debug.h"

// defines
#define ESS_PSTACK_ALIGN_BYTE		sizeof(t_int8*)				//!< align byte
#define ESS_PSTACK_ALIGN_BITS		_gdwPStackAlignBits			//!< align bits
#define ESS_PSTACK_ALIGN_MASK		(ESS_PSTACK_ALIGN_BYTE - 1)	//!< align byte mask

static t_int32	_gdwPStackAlignBits;

// static functions

/**
 * This function initialize PSTACK module and create a default PSTACK
 *
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 * @version		AUG-10-2009 [JW Park] Add consideration of 64 bit host.
 */
t_int32
EssPStack_Init(void)
{
	ESS_PSTACK_ALIGN_BITS = EssMath_Log2(ESS_PSTACK_ALIGN_BYTE);

	return ESS_OK;
}


/**
 * This function initialize PSTACK Main structure
 *
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
t_int32
EssPStack_InitMain(EssPStackMain* pPM)
{
	// initialize PSTACK main structure
	ESS_DLIST_INIT(&pPM->stListFree);
	ESS_DLIST_INIT(&pPM->stListBusy);
	pPM->pDefaultPStack	= NULL;

	return ESS_OK;
}


/**
 * This function add a default PSTACK
 *
 * @param		pPM		: PStack Main structure
 * @param		pBuff	: buffer pointer for PSTACK
 * @param		dwSize	: buffer size
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
t_int32
EssPStack_AddDefault(EssPStackMain* pPM, t_int8* pBuff, t_int32 dwSize)
{
	t_int32		r;

	ESS_ASSERT(pPM);

	if (pPM->pDefaultPStack != NULL)
	{
		ESS_LOG_PRINTF("a default PSTACK was already registered");
		return ESS_EERROR;
	}

	r = ESSPStack_initPStack(&pPM->pDefaultPStack, pBuff, dwSize);
	if (r < 0)
	{
		ESS_LOG_PRINTF("Fail to initialize default PSTACK");
		return r;
	}

	return ESS_OK;
}


/**
* This function initialize and sets PSTACK information at ppPS
*
* @param		pBuff	: buffer pointer for PSTACK
* @param		dwSize	: buffer size
* @return		ESS_OK		: success
* @return		else		: error number
* @author		DongYoung Seo
* @version		JUL-27-2006 [DongYoung Seo] First Writing.
* @version		AUG-10-2009 [JW Park] Add consideration of 64 bit host.
*/
t_int32
ESSPStack_initPStack( EssPStack** ppPS, t_int8* pBuff, t_int32 dwSize )
{
	ESS_ASSERT(ppPS);

	if ((t_uint)pBuff & ESS_PSTACK_ALIGN_MASK)
	{
		ESS_LOG_PRINTF("PSTACK is misaligned. Please report it");
		return ESS_EERROR;
	}

	if (dwSize < sizeof(EssPStack))
	{
		ESS_LOG_PRINTF("Invalid parameter, Too small memory for PStack \n");
		return ESS_EINVALID;
	}
	
	(*ppPS) = (EssPStack*)pBuff;
	(*ppPS)->dwMaxDepth		= (dwSize - sizeof(EssPStack)) >> ESS_PSTACK_ALIGN_BITS;
	(*ppPS)->dwDepth		= 0;
	(*ppPS)->pHeapData		= (t_int8*)(pBuff + sizeof(EssPStack));
	ESS_DLIST_INIT(&(*ppPS)->stList);

	return ESS_OK;
}


/**
 * This function return a default PStack
 *
 * @param		pPM		: PStack Main structure
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
EssPStack*
EssPStack_GetDefault(EssPStackMain* pPM)
{
	ESS_ASSERT(pPM);

	return pPM->pDefaultPStack;
}


/**
* This function remove default PStack
*
* @param		pPM		: PStack Main structure
* @return		ESS_OK		: success
* @return		else		: error number
* @author		DongYoung Seo
* @version		MAR-13-2007 [DongYoung Seo] First Writing.
* @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
*/
t_int32
EssPStack_RemoveDefault(EssPStackMain* pPM)
{
	ESS_ASSERT(pPM);

	pPM->pDefaultPStack = NULL;
	return ESS_OK;
}


/**
 * This function gets a free PStack
 *
 * @param		pPM				: PStack Main structure
 * @param		dwSize			: PSTACK size
 * @return		positive ptr	: a free PSTACK
 * @return		NULL			: there is no free PStack
 * @author		DongYoung Seo
 * @version		SEP-25-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
EssPStack*
EssPStack_GetFree(EssPStackMain* pPM, t_uint32 dwSize)
{
	EssPStack*	pPS;
	EssPStack*	pPSNext;	// next PStack

	ESS_ASSERT(pPM);

	if (ESS_DLIST_IS_EMPTY(&pPM->stListFree) == ESS_TRUE)
	{
		// there is no free PStack
		return NULL;
	}

	dwSize = dwSize >> ESS_PSTACK_ALIGN_BITS;

	ESS_DLIST_FOR_EACH_ENTRY_SAFE(EssPStack, pPS, pPSNext, &pPM->stListFree, stList)
	{
		if (pPS->dwMaxDepth >= dwSize)
		{
			ESS_DLIST_DEL_INIT(&pPS->stList);
			return pPS;
		}
	}

	return NULL;
}


/**
 * This function releases a PStack and put it to free list
 *
 * @param		pPM			: PStack Main structure
 * @param		pPStack		: buffer pointer for cache
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		SEP-25-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
t_int32
EssPStack_Release(EssPStackMain* pPM, EssPStack* pPStack)
{
	// release a PSTACK and add to free list
	EssPStack*	pPS;
	EssPStack*	pPSNext;	// next PStack

	if (pPStack == NULL)
	{
		return ESS_EINVALID;
	}

	ESS_ASSERT(pPStack);

	pPStack->dwDepth = 0;

	ESS_DLIST_FOR_EACH_ENTRY_SAFE(EssPStack, pPS, pPSNext, &pPM->stListFree, stList)
	{
		if (pPS->dwMaxDepth > pPSNext->dwMaxDepth)
		{
			continue;
		}

		ESS_DLIST_ADD(&pPS->stList, &pPSNext->stList, &pPStack->stList);

		return ESS_OK;
	}

	ESS_DLIST_ADD_TAIL(&pPM->stListFree, &pPStack->stList);

	return ESS_OK;
}


/**
 * This function add a new PSTACK
 *
 * @param		pPM			: PStack Main structure
 * @param		pBuff		: buffer pointer for PSTACK
 * @param		dwSize		: buffer size
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 */
t_int32
EssPStack_Add(EssPStackMain* pPM, t_int8* pBuff, t_int32 dwSize)
{
	EssPStack*	pPS;
	t_int32		r;

	r = ESSPStack_initPStack(&pPS, pBuff, dwSize);
	if (r < 0)
	{
		ESS_LOG_PRINTF("Fail to initialize default PSTACK");
		return r;
	}

	// insert PSTACK with order of max depth
	EssPStack_Release(pPM, pPS);	// add it to free list

	return ESS_OK;
}


/**
 * This function removes a PSTACK from PSTACK list
 *
 * @param		pPStack: buffer pointer for cache
 * @return		ESS_OK		: success
 * @return		else		: error number
 * @author		DongYoung Seo
 * @version		JUL-27-2006 [DongYoung Seo] First Writing.
 */
t_int32
EssPStack_Remove(EssPStack* pPStack)
{
	ESS_ASSERT(pPStack);

	if ((pPStack->stList.pNext != NULL) && (pPStack->stList.pPrev != NULL))
	{
		ESS_DLIST_DEL_INIT(&pPStack->stList);
	}
	else
	{
		ESS_DEBUG_PRINTF("invalid pPStack pointer");
		return ESS_EINVALID;
	}

	return ESS_OK;
}


/**
 * This function allocate memory from PSTACK
 *
 * @param		pPM			: PStack Main structure
 * @param		pPStack		: PStack pointer for cache
 * @param		dwSize		: memory allocation size in byte
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREADY	: FFATFS already initialized
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 * @version		AUG-10-2009 [JW Park] Add consideration of 64 bit host.
 */
void*
EssPStack_Alloc(EssPStackMain* pPM, EssPStack* pPStack, t_int32 dwSize)
{
	t_int32		dwDepthNeeded;
	t_int8*		pRet;
	EssPStack*	pPS;

	ESS_ASSERTP(dwSize != 0, "prog error, size is 0");

	if (pPStack == NULL)
	{
		pPS = EssPStack_GetDefault(pPM);
	}
	else
	{
		pPS = pPStack;
	}

	dwDepthNeeded = ESS_MATH_CDB(dwSize, ESS_PSTACK_ALIGN_BYTE, ESS_PSTACK_ALIGN_BITS);

	// check if there are enough memory
	if ((pPS->dwDepth + dwDepthNeeded) > pPS->dwMaxDepth)
	{
		ESS_DEBUG_PRINTF("not enough memory!\n");
		ESS_ASSERT(0);
		return NULL;
	}

	pRet = pPS->pHeapData + (pPS->dwDepth << ESS_PSTACK_ALIGN_BITS);
	pPS->dwDepth += dwDepthNeeded;

	return pRet;
}


/**
 * This function allocate memory from PSTACK
 *
 * @param		pPM		: PStack Main structure
 * @param		pPStack		: pPStack pointer for cache
 * @param		p			: memory pointer
 * @param		dwSize		: memory size in byte
 * @return		FFAT_OK				: success
 * @return		FFAT_EINIT_ALREAD	: FFATFS already initialized
 * @author		DongYoung Seo
 * @version		JUL-06-2006 [DongYoung Seo] First Writing.
 * @version		DEC-04-2007 [DongYoung Seo] add parameter pPM to increase flexibility
 * @version		AUG-10-2009 [JW Park] Add consideration of 64 bit host.
 */
void
EssPStack_Free(EssPStackMain* pPM, EssPStack* pPStack, void* p, t_int32 dwSize)
{
	t_int32		dwDepthNeeded;
	void*		pPtr;
	EssPStack*	pPS;

	ESS_ASSERT(p);
	ESS_ASSERT(dwSize > 0);

	if (pPStack == NULL)
	{
		pPS = EssPStack_GetDefault(pPM);
	}
	else
	{
		pPS = pPStack;
	}

	dwDepthNeeded = ESS_MATH_CDB(dwSize, ESS_PSTACK_ALIGN_BYTE, ESS_PSTACK_ALIGN_BITS);

	ESS_ASSERT(pPS->dwDepth >= (t_uint32)dwDepthNeeded);

	// check object pointer
	pPtr = pPS->pHeapData + ((pPS->dwDepth - dwDepthNeeded) << ESS_PSTACK_ALIGN_BITS);

	if (pPtr != p)
	{
		ESS_DEBUG_PRINTF("different pointer address!\n");
		ESS_ASSERT(0);
		while(1);
	}

	pPS->dwDepth -= dwDepthNeeded;

	return;
}

//=============================================================================
//
//	static functions
//



