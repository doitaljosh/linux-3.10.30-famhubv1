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
 * @file		ess_bitmap.c
 * @brief		module for bitmap operation
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		AUG-16-2006 [DongYoung Seo] First writing
 * @see			None
 */


#include "ess_bitmap.h"
#include "ess_debug.h"



// this bitmap is used to get lowest 1 bit int a byte variable.
// -1	: there is no 1 bit
static const t_int8	pBitmapLowestBitOne[] =
{
	-1, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,		// 0 - 15
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 16 - 31
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 32 - 47
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 48 - 63
	6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 64 - 79
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 80 - 95
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 96 - 111
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 112 - 127
	7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 128 - 143
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 144 - 159
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 160 - 175
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 176 - 191
	6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 192 - 207
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 208 - 223
	5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 224 - 239
	4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,			// 240 - 255
};


// this bitmap is used to get lowest 1 bit int a byte variable.
// -1	: there is no 0 bit
static const t_int8	pBitmapLowestBitZero[] =
{
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 0 - 15
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,			// 16 - 31
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 32 - 47
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,			// 48 - 63
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 64 - 79
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,			// 80 - 95
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 96 - 111
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,			// 112 - 127
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 128 - 143
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,			// 144 - 159
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 160 - 175
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,			// 176 - 191
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 192 - 207
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,			// 208 - 223
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,			// 224 - 239
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, -1			// 240 - 255
};


// this bitmap is used to get lowest 1 bit int a byte variable.
// -1	: there is no 0 bit
static const t_int8	pBitmapCountOfBitOne[] =
{
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,			// 0 - 15
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,			// 16 - 31
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,			// 32 - 47
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 48 - 63
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,			// 64 - 79
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 80 - 95
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 96 - 111
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,			// 112 - 127
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,			// 128 - 143
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 144 - 159
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 160 - 175
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,			// 176 - 191
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,			// 192 - 207
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,			// 208 - 223
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,			// 224 - 239
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8			// 240 - 255
};


/**
 * get the lowest bit 0
 *
 * @param		pBitmap	: [IN] bitmap pointer
 * @param		dwSize	: [IN] bitmap size in byte
 * @return		between 0 to (dwSize * 8 -1) : the lowest zero bit.
 * @return		ESS_ENOENT		: there is no zero bit
 * @return		ESS_EINVALID	: invalid parameter
 * @author		DongYoung Seo
 * @version		AUG-16-2006 [DongYoung Seo] First Writing
 * @version		MAY-08-2007 [DongYoung Seo] modify error value. on error.
 * @version		JUN-16-2008 [DongYoung Seo] adapt bitmap table
 */
t_int32
EssBitmap_GetLowestBitZero(t_uint8* pBitmap, t_int32 dwSize)
{
	t_int32		i;
	t_uint32*	pBuff32;			// pointer for 32bit
	t_int32		dwCount32;			// count to check 4byte stride walk

#ifdef ESS_BITMAP_STRICT_CHECK
	if ((pBitmap == NULL) || (dwSize <= 0))
	{
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pBitmap);
	ESS_ASSERT(dwSize > 0);

	// check aligned or not aligned
	if (((t_uint)pBitmap & 0x03) == 0)
	{
		// first step, reach to the bit 0 with stride walk
		pBuff32		= (t_uint32*)pBitmap;
		dwCount32	= dwSize / sizeof(t_uint32);

		for (i = 0; i < dwCount32; i++)
		{
			if (pBuff32[i] != 0xFFFFFFFF)
			{
				break;
			}
		}
	}
	else
	{
		i = 0;
	}

	i = i * 4;

	// sequential search.
	for (/* None */; i < dwSize; i++)
	{
		// check is there any empty slot
		if (pBitmap[i] != (t_uint8)0xFF)
		{
			ESS_ASSERT(pBitmapLowestBitZero[pBitmap[i]] >= 0);
			ESS_ASSERT(pBitmapLowestBitZero[pBitmap[i]] < 8);

			return ((i * 8) + pBitmapLowestBitZero[pBitmap[i]]);
		}
	}

	// there is no zero bit
	return ESS_ENOENT;		// there is no zero bit
}

 
/**
* get the lowest bit 1
*
* @param		pBitmap		: [IN] bitmap pointer
* @param		dwSize		: [IN] bitmap size in byte
* @return		between 0 to (dwSize * 8 -1) : the lowest one bit.
* @return		ESS_ENOENT		: there is no zero bit
* @return		ESS_EINVALID	: invalid parameter
* @author		DongYoung Seo
* @version		MAY-28-2007 [DongYoung Seo] first writing.
* @version		JUN-16-2008 [DongYoung Seo] adapt bitmap table
*/
t_int32
EssBitmap_GetLowestBitOne(t_uint8* pBitmap, t_int32 dwSize)
{
	t_int32		i;
	t_uint32*	pBuff32;			// pointer for 32bit
	t_int32		dwCount32;			// count to check 4byte stride walk

#ifdef ESS_BITMAP_STRICT_CHECK
	if ((pBitmap == NULL) || (dwSize <= 0))
	{
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pBitmap);
	ESS_ASSERT(dwSize > 0);

	// check aligned or not aligned
	if (((t_uint)pBitmap & 0x03) == 0)
	{
		// first step, reach to the bit 1 with stride walk
		pBuff32		= (t_uint32*)pBitmap;
		dwCount32	= dwSize / sizeof(t_uint32);

		for (i = 0; i < dwCount32; i++)
		{
			if (pBuff32[i] != 0)
			{
				break;
			}
		}
	}
	else
	{
		i = 0;
	}

	i = i * 4;

	// sequential search.
	for (/* None */; i < dwSize; i++)
	{
		// check is there any empty slot
		if (pBitmap[i] != 0x00)
		{
			ESS_ASSERT(pBitmapLowestBitOne[pBitmap[i]] >= 0);
			ESS_ASSERT(pBitmapLowestBitOne[pBitmap[i]] < 8);
			return ((i * 8) + pBitmapLowestBitOne[pBitmap[i]]);
		}
	}

	// there is no bit 1
	return ESS_ENOENT;
}


/**
 * get the count of bit 1
 *
 * @param		pBitmap	: [IN] bitmap pointer
 * @param		dwSize	: [IN] bitmap size in byte
 * @return		count of bit 1 int the bitmap
 * @author		DongYoung Seo
 * @version		JUN-16-2008 [DongYoung Seo] first writing
 */
t_int32
EssBitmap_GetCountOfBitOne(t_uint8* pBitmap, t_int32 dwSize)
{
	t_int32		i;
	t_int32		dwCount;

#ifdef ESS_BITMAP_STRICT_CHECK
	if ((pBitmap == NULL) || (dwSize <= 0))
	{
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pBitmap);
	ESS_ASSERT(dwSize > 0);

	dwCount = 0;

	// loop unrolling will be done by compiler
	for (i = 0; i < dwSize; i++)
	{
		dwCount += pBitmapCountOfBitOne[pBitmap[i]];
	}

	return dwCount;
}


