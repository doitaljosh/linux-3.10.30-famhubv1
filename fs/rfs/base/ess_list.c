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
 * @file		ess_list.h
 * @brief		Single linked list
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

// include for STD C

// include for ESS 
#include "ess_types.h"
#include "ess_list.h"

// debug begin
#include "ess_debug.h"
// debug end



/** 
 * @brief	This function count entry count
 * @param	pHead : list pHead to be checked
 * @return  	number of nodes
*/
t_int32
EssList_Count(EssList* pHead)
{
	t_int32		dwCount;
	EssList*	pNext;

	ESS_ASSERT(pHead);

	dwCount = 0;
	pNext = pHead->pNext;
	while (pNext != NULL)
	{
		dwCount++;
		pNext = pNext->pNext;
	}

	return dwCount;
}

