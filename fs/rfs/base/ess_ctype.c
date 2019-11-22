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
 * @file		ess_ctype.c
 * @brief		migration from FSM/LIB/util.c
 * @author		Soojeong kim(sj003.kim@samsung.com)
 * @version	  	JEN-08-2007 [Soojeong Kim] First writing
 * @see			None
 */

#include "ess_ctype.h"
#include "ess_debug.h"

/*  purpose : convert numeric string to integer
	input :
		str : numerical string
	output :
		0 >		   : converted number
		TFS4_ERROR : error
	note :
	revision history :
		13-MAR-2004 [DongYoung Seo]: First writing
		21-SEP-2005 [Joone Hur]: rename atol to strtol
		29-SEP-2005 [Chankyu Kim]: rename strtol to strtoi
*/

#define ESS_TOLOWER(x)      ((((x) >= 'A') && ((x) <= 'Z')) ? ((x) + 0x20) : (x))

t_int32
ESS_Atoi(t_int8* str)
{
	t_uint32    val;
	t_int32     base, c;
	t_int8*		sp;

	ESS_ASSERT(str != NULL);

	val = 0;
	sp = str;
	if ((*sp == '0') && ((*(sp+1) == 'x') || (*(sp+1) == 'X')) )
	{
		base = 16;
		sp += 2;
	}
	else if (*sp == '0')
	{
		base = 8;
		sp++;
	}
	else
	{
		base = 10;
	}

	for (; (*sp != 0); sp++)
	{
		c = (*sp > '9') ? (ESS_TOLOWER(*sp) - 'a' + 10) : (*sp - '0');
		val = (val * base) + c;
	}
	return(val);
}


/*  purpose :  Convert an integer to a string.

	input :
		nVal: integer type value
		str : string buffer
	output :
		converted number
	note :
		support only 10 radix
	revision history :
		21-SEP-2005 [Joone Hur]:  First writing
*/
t_int8*
ESS_Itoa(t_int32 nVal, t_int8* str)
{
	t_int32		bMinus;
	t_int32		nMaxColumn = 1000000000;
	t_int32		nMaxCol = 11;

	ESS_ASSERT(str != NULL);

	if (nVal < 0)
	{
		bMinus = 1;
		nVal = -nVal;
		nMaxCol++;
	}
	else
	{
		bMinus = 0;
	}

	if (nVal == 0)
	{
		nMaxCol = 2;
	}
	else
	{
		while (nMaxColumn)
		{
			if (nVal >= nMaxColumn)
			{
				break;
			}

			nMaxCol--;
			nMaxColumn /= 10;
		}
	}

	str[--nMaxCol] = '\0';

	if (nVal == 0)
	{
		str[--nMaxCol] = '0';
	}
	else
	{
		while (nVal > 0)
		{
			str[--nMaxCol] = (t_int8)(nVal % 10) +'0';
			nVal /= 10;
		}
	}

	if (bMinus)
	{
		str[--nMaxCol] = '-';
	}

	return str;
}


/*  purpose : chkeck alphabet or num
			  std lib sometimes does not return correct value
	input :
		c : a char for check
	output :
		true : alphabet or numerical char
		fale : not alphabet or numerical char
	note :
	revision history :
		11-MAR-2004 [DongYoung Seo]: First writing
*/
t_int32
ESS_Isalnum(t_int8 c)
{
	if (
		((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9'))
	)
	{
		return  ESS_TRUE;
	}
	return  ESS_FALSE;
}


/*	purpose : check if the two strings are same (case-insensitive)
	input :
		s1 : first string
		s2 : second string
	output :
		1 if they are different
		0 if they are same
	note :
	revision history :
*/
t_int32
ESS_Stricmp(const t_int8* s1, const t_int8* s2)
{
	t_uint8		c1, c2;

	ESS_ASSERT(s1 != NULL);
	ESS_ASSERT(s2 != NULL);

	while (*s1 != '\0')
	{
		c2 = *s2++;
		if (c2 == '\0')
		{
			return 1;
		}
		c1 = *s1++;

		if (c1 == c2)
		{
			continue;
		}

		c1 = (t_uint8)ESS_TOLOWER(c1);
		c2 = (t_uint8)ESS_TOLOWER(c2);

		if (c1 != c2)
		{
			return (c1 - c2);
		}
	}

	if (*s2 != '\0')
	{
		return 1;
	}

	return 0;
}

