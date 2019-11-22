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
 * @file		ess_math.h
 * @brief		operation that relatives with Mathmatics
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-17-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_math.h"

/**
 * This function checks if Value is power of 2 or not
 *
 * @param		dwValue	 : number
 * @return		ESS_TRUE	: if dwValue is power of 2
 * @return		ESS_FALSE	: if dwValue is not power of 2
 * @author		DongYoung Seo
 * @version		JUL-17-2006 [DongYoung Seo] : Migration from TFS4 1.6, tfs4_util.c
 * @version		JUL-25-2006 [DongYoung Seo] : new algorithm from Qing Zhang
*/
t_boolean
EssMath_IsPowerOfTwo(t_uint32 dwValue)
{
	return (dwValue & (dwValue - 1)) ? ESS_FALSE : ESS_TRUE;
}


/**
 * This function returns log_2(dwValue)
 *
 * @param		dwValue	: log value 
 * @return		0 - 32	: bit count of dwValue
 * @return		-1		: error or too big number
 * @author		DongYoung Seo
 * @version		JUL-18-2006 [DongYoung Seo] Migration from TFS4 1.6, tfs4_util.c
*/
t_int32
EssMath_Log2(t_uint32 dwValue)
{
	t_int32		r, i;
	t_uint32	dwOrg;

	r = -1;

	if (dwValue == 0)
	{
		return 0;
	}
	dwOrg = dwValue;

	i = 1;
	// find first '1'
	while ((dwValue & 0x00000001) == 0)
	{
		dwValue = dwValue >> 1;
		i++;
	}

	if (dwOrg == (0x01U << (i - 1)))
	{
		return (i - 1);
	}
	else
	{
		return r;
	}
}

#ifdef ESS_MATH_DEBUG
	/**
	 *	purpose : ceiling divide with bit information
	 *	@param		dwA : numerator
	 *	@param		dwB : denominator
	 *	@param		dwC : bit length of dwB
	 *	@return		ceiling divide
	 * @author		DongYoung Seo
	 * @version		JUL-23-2006 [DongYoung Seo] Migration from TFS4 1.6, tfs4_util.c
					MAY-18-2009 [Junseop Jeong] Supporting 4GB file
	*/
	t_uint32
	EssMath_CeilingDivideWithBit(t_uint32 dwA, t_uint32 dwB, t_uint32 dwC)
	{
		t_uint32 dwR;

		if (dwA & (dwB - 1))
		{
			dwR = dwA >> dwC;
			dwR++;
		}
		else
		{
			dwR = dwA >> dwC
		}

		return dwR;
	}

	/**
	 *	purpose : ceiling divide
	 *	@param		dwA : numerator
	 *	@param		dwB : denominator
	 *	@return		ceiling divide
	 * @author		DongYoung Seo
	 * @version		JUL-23-2006 [DongYoung Seo] Migration from TFS4 1.6, tfs4_util.c
					MAY-18-2009 [Junseop Jeong] Supporting 4GB file
	*/
	t_uint32
	EssMath_CeilingDivide(t_uint32 dwA, t_uint32 dwB)
	{
		t_uint32 dwR;

		if (dwA % dwB)
		{
			dwR = dwA / dwB;
			dwR++;
		}
		else
		{
			dwR = dwA / dwB;
		}

		return dwR;
	}
#endif	// end of


// random function

static t_int32	_dwRandCurState = 0L;

/**
* This function returns a random value
*
* @return		a random value
*/
t_int32
EssMath_Rand(void)
{
	return((_dwRandCurState = (_dwRandCurState * 1103515245L + 12345L)) & 0x7fffffffL);
}

/**
* This function sets seed of random
*
* @param	dwNew : new random seed
*/
void
EssMath_Srand(t_int32 dwNew)
{
	_dwRandCurState = dwNew;
}


