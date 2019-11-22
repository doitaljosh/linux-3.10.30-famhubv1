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
* @file			ess_unicode.c
* @brief		This file impelements functions for UNICODE operation.
* @author		DongYoung Seo(dy76.seo@samsung.com)
* @version		JUL-21-2006 [DongYoung Seo] First writing
* @see			None
*/

#include "ess_endian.h"
#include "ess_unicode.h"

//definition for Unicode (DUL enable or disable)
// code pages
extern struct sbcs_table cp_1250;
extern struct sbcs_table cp_1251;
extern struct sbcs_table cp_1252;
extern struct sbcs_table cp_1253;
extern struct sbcs_table cp_1254;
extern struct sbcs_table cp_1255;
extern struct sbcs_table cp_1256;
extern struct sbcs_table cp_1257;
extern struct sbcs_table cp_1258;

extern struct sbcs_table cp_28591;
extern struct sbcs_table cp_28592;
extern struct sbcs_table cp_28593;
extern struct sbcs_table cp_28594;
extern struct sbcs_table cp_28595;
extern struct sbcs_table cp_28596;
extern struct sbcs_table cp_28597;
extern struct sbcs_table cp_28598;
extern struct sbcs_table cp_28599;
extern struct sbcs_table cp_28605;

extern struct sbcs_table cp_437;
extern struct sbcs_table cp_852;
extern struct sbcs_table cp_855;
extern struct sbcs_table cp_857;
extern struct sbcs_table cp_858;
extern struct sbcs_table cp_862;
extern struct sbcs_table cp_866;
extern struct sbcs_table cp_874;

extern struct sbcs_table cp_737;
extern struct sbcs_table cp_775;
extern struct sbcs_table cp_850;

extern struct dbcs_table cp_932;
extern struct dbcs_table cp_936;
extern struct dbcs_table cp_949;
extern struct dbcs_table cp_950;

struct cptable_entry
{
	char *name;
	union sd_table
	{
		struct sbcs_table *sbcs_tab;
		struct dbcs_table *dbcs_tab;
	}*table;
};

struct cptable_entry cptable_array[] =
{ 
	{"1251", (void *)&cp_1251},
	{"1252", (void *)&cp_1252},
	{"1253", (void *)&cp_1253},
	{"1254", (void *)&cp_1254},
	{"1255", (void *)&cp_1255},
	{"1256", (void *)&cp_1256},
	{"1257", (void *)&cp_1257},
	{"1258", (void *)&cp_1258},

	{"28591", (void *)&cp_28591},
	{"28592", (void *)&cp_28592},
	{"28593", (void *)&cp_28593},
	{"28594", (void *)&cp_28594},
	{"28595", (void *)&cp_28595},
	{"28596", (void *)&cp_28596},
	{"28597", (void *)&cp_28597},
	{"28598", (void *)&cp_28598},
	{"28599", (void *)&cp_28599},
	{"28605", (void *)&cp_28605},

	{"437", (void *)&cp_437},
	{"852", (void *)&cp_852},
	{"855", (void *)&cp_855},
	{"857", (void *)&cp_857},
	{"858", (void *)&cp_858},
	{"862", (void *)&cp_862},
	{"866", (void *)&cp_866},
	{"874", (void *)&cp_874},

	{"737", (void *)&cp_737},
	{"775", (void *)&cp_775},
	{"850", (void *)&cp_850},

	{"932", (void *)&cp_932},
	{"936", (void *)&cp_936},
	{"949", (void *)&cp_949},
	{"950", (void *)&cp_950},
};

#define __CODEPAGE_TABLE(no) (union cp_table *)(&cp_##no)
#define _CODEPAGE_TABLE(no) __CODEPAGE_TABLE(no)

static union		cp_table*	cptable = _CODEPAGE_TABLE(ESS_CODEPAGE);	// current codepage is cp_##ESS_CODEPAGE

#define _LOWER_SIZE_IN_BYTE					5884
#define _UPPER_SIZE_IN_BYTE					5988
#define _WCTYPE_IN_BYTE						29184

extern const t_uint16 casemap_lower[_LOWER_SIZE_IN_BYTE / sizeof(t_uint16)];
extern const t_uint16 casemap_upper[_UPPER_SIZE_IN_BYTE / sizeof(t_uint16)];
extern const t_uint16 wctype_table[_WCTYPE_IN_BYTE / sizeof(t_uint16)];

static t_uint32 _mbstowcs_dbcs(t_uint16* wcs, const t_int8* mbs, t_uint32 cch);
static t_uint32 _mbstowcs_sbcs(t_uint16* wcs, const t_int8* mbs, t_uint32 cch);

static t_int32 _mbtowc_dbcs(t_uint16* wc, const t_int8* mbc, t_uint32 cch);
static t_int32 _mbtowc_sbcs(t_uint16* wc, const t_int8* mbc, t_uint32 cch);

static t_uint32 _get_mb_num_dbcs(const t_int8* mbs);
static t_uint32 _get_mb_num_sbcs(const t_int8* mbs);

static t_uint32 _is_invalid_mbc_dbcs(const t_int8* mbc, t_uint32 count);

static t_uint32 _wcstombs_dbcs(t_int8* mbs, const t_uint16* wcs, t_uint32 count);
static t_uint32 _wcstombs_sbcs(t_int8* mbs, const t_uint16* wcs, t_uint32 count);

static t_int32 _wctomb_dbcs(t_int8* mbchar, t_uint16 wchar);
static t_int32 _wctomb_sbcs(t_int8* mbchar, t_uint16 wchar);

static t_uint32 _get_wc_num_dbcs(const t_uint16* wcs);
static t_uint32 _get_wc_num_sbcs(const t_uint16* wcs);

static t_int32 _wchartodigit(t_uint16 ch);


t_int32
Unicode_Init(void)
{
	cptable = _CODEPAGE_TABLE(ESS_CODEPAGE); //cptable = &cp_##ESS_CODEPAGE;

	return 0;
}

/*  purpose : Set the Unicode CP table as per the request
	input
		pwszCodePage : Null-terminated string
	output
		
	note
	revision history :
		13-Aug-2010 [Rajshekar Payagond]: First writing.
*/
t_int32
Unicode_Set_Codepage(t_int8* pwszCodePage)
{
	t_int32 i;

	for(i = 0; i < (sizeof(cptable_array)/sizeof(struct cptable_entry)); i++)
	{
		/* Compare the Codepage string, if matches set cptable accordingly */
		if(strcmp(pwszCodePage, cptable_array[i].name) == 0)
		{
			cptable = (union cp_table *)cptable_array[i].table;
			break;
		}
	}
	return 0;
}

/*  purpose : Get the length of a string.
	input
		wcs : Null-terminated string
	output
		The number of characters in string, excluding the NULL.
	note
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		07-Sep-2004 [Shin, Hee-Sub]: Byte alignment.
		31-JAN-2005 [Kwon, Moon-Sang]: Modify () and divide by 2 to shift (>>) operation
*/
t_uint32
uni_wcslen(const t_uint16* wcs)
{
	if (!((t_int32)wcs & 0x1)) /* 2-byte aligned */
	{
		t_uint16*	temp = (t_uint16 *) wcs;

		while (*temp++)
		{
			/* DO NOTHING */;
		}

		return ((t_uint32)(temp - wcs - 1));
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		t_uint8*	temp = (t_uint8 *) wcs;

		while ((*temp) || (*(temp + 1)))
		{
			temp += 2;
		}

		return (((t_uint32)(temp - (t_uint8 *) wcs)) >> 1);
	}
}


/*  purpose : Copy a UNICODE string.
input
dst : destination string
wcs : Null-terminated source string
output
the destination string
note
revision history
25-Aug-2004 [Shin, Hee-Sub]: First writing.
07-Sep-2004 [Shin, Hee-Sub]: Byte alignment.
06-Apr-2005 [Shin, Hee-Sub]:
A critical defect that may cause the result to be wrong
when the arguments are not properly aligned was found and fixed.
*/
t_uint16 *
uni_wcscpy(t_uint16* dst, const t_uint16* src)
{
	if (!((t_uint)dst & 0x1)  && !((t_uint)src & 0x1)) // 2-byte aligned
	{
		t_uint16*	temp = dst;

		// until a terminate-null appears
		while ((*temp++ = *src++) != 0)
		{
			/* DO NOTHING */;
		}
	}
	else // not-aligned, byte to byte approach - slow
	{
		t_int8*	pc1 = (t_int8 *) dst;
		t_int8*	pc2 = (t_int8 *) src;

		do
		{
			*pc1++ = *pc2++;
			*pc1++ = *pc2++;

		} while ((*(pc2 - 2) != 0) || (*(pc2 - 1) != 0));
	}

	return(dst);
}


/*  purpose : Copy a UNICODE string only for a given number of character.
input
dst : destination string
wcs : source string
cch : the number of characters to be copied

output
the destination string
note
revision history :
25-Aug-2004 [Shin, Hee-Sub]: First writing.
07-Sep-2004 [Shin, Hee-Sub]: Byte-alignment.
*/
t_uint16 *
uni_wcsncpy (t_uint16* dst, const t_uint16* src, t_uint32 cch)
{
	// <ANSI standard>
	// 1. If the length of source string is shorter than cch, then the
	//    remaining space of the destination string should be filled with
	//    zero.
	// 2. The destination string may not be terminated by NULL, if cch is
	//    equal to or less than the length of the source string.

	if (!((t_uint)dst & 0x1) && !((t_uint)src & 0x1)) /* 2-byte aligned */
	{
		t_uint16*	temp = dst;

		while (cch && ((*temp++ = *src++) != 0x0))
		{
			cch--;
		}

		if (cch) // <ANSI Standard> Case #1
		{
			while (--cch)
			{
				// fill remaining spaces of the destination with zero
				*temp++ = 0;
			}
		}

		return(dst);
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		t_int8*	pc1 = (t_int8 *) dst;
		t_int8*	pc2 = (t_int8 *) src;

		// until cch becomes zero or a terminate-null appears
		while (cch)
		{
			*pc1++ = *pc2++;
			*pc1++ = *pc2++;

			if ((*(pc2 - 2) == 0) &&
				(*(pc2 - 1) == 0))
			{
				// got the terminate-null
				break;
			}

			cch--;
		}

		if (cch) // <ANSI Standard> Case #1
		{
			while (--cch)
			{
				// fill remaining spaces of the destination with zero
				*pc1++ = 0;
				*pc1++ = 0;
			}
		}

		return (dst);
	}
}


/*  purpose : a lowercase comparison of UNICODE strings.
input
s1  : first string
s2  : second string
output
<0 - if dst <  src
0 - if dst == src
>0 - if dst >  src
note
revision history
25-Aug-2004 [Shin, Hee-Sub]: First writing.
07-Sep-2004 [Shin, Hee-Sub]: Byte-alignment.
06-Apr-2005 [Shin, Hee-Sub]:
A critical defect that may cause the comparison to be wrong
when the arguments are not properly aligned was found and fixed.
06-Apr-2005 [Shin, Hee-Sub]:
On the platform that uses big-endian, the result of comparison
between two different strings was incorrect.
*/
t_int32
uni_wcsicmp(const t_uint16* wcs1, const t_uint16* wcs2, t_boolean bByteSwap)
{
	t_uint16		f, l;

	f = l = 0;

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) /* 2-byte aligned */
	{
		do
		{
			if (*wcs1 != *wcs2)
			{
				f = uni_towlower((t_uint16)(*wcs1));
				l = uni_towlower((t_uint16)(*wcs2));
			}
			else
			{
				f = l = *wcs1;
			}

			wcs1++; wcs2++;
		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}
	else /* not-aligned */
	{
		t_uint8*	pc1 = (t_uint8 *) wcs1;
		t_uint8*	pc2 = (t_uint8 *) wcs2;

		t_uint16 wc1, wc2;

		do
		{
			// we don't have to worry about following two local variables. because,
			// they are optimized by the compiler and excluded from the final binary.
			wc1 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc1, *(pc1 + 1), bByteSwap);
			wc2 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc2, *(pc2 + 1), bByteSwap);

			// convert them to lower case
			if (wc1 != wc2)
			{
				f = uni_towlower(wc1);
				l = uni_towlower(wc2);
			}
			else
			{
				f = l = wc1;
			}

			pc1 += 2;
			pc2 += 2;
		} while ((f) && (f == l));
		// until an invalid character appears or two characters are different.
	}

	return (t_int32)(f - l);
}

/*  purpose : compares two UNICODE strings.
input
wcs1    : first string
wcs2    : second string
cch     : the initial number of characters to compare
output
<0 if wcs1 <  wcs2
0 if wcs1 == wcs2
>0 if wcs2 >  wcs2
note
revision history
25-Aug-2004 [Shin, Hee-Sub]: First writing.
06-Apr-2005 [Shin, Hee-Sub]:
A critical defect that may cause the comparison to be wrong
when the arguments are not properly aligned was found and fixed.
06-Apr-2005 [Shin, Hee-Sub]:
On the platform that uses big-endian, the result of comparison
between two different strings was incorrect.
*/
t_int32
uni_wcsncmp(const t_uint16* wcs1, const t_uint16* wcs2, t_uint32 cch, t_boolean bByteSwap)
{
	// if cch equals to zero, we don't have to go further.
	if (!cch)
	{
		return 0;
	}

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) // 2-byte aligned
	{
		// until cch becomes zero or any difference is found.
		while ((--cch) && (*wcs1) && (*wcs1 == *wcs2))
		{
			wcs1++; wcs2++;
		}

		// return value should mean lexicographic relation between two different characters.
		return((t_int32)(*wcs1 - *wcs2));
	}
	else // not-aligned, byte to byte approach - slow
	{
		t_uint8*	pc1 = (t_uint8 *) wcs1;
		t_uint8*	pc2 = (t_uint8 *) wcs2;
		t_uint16		wc1;
		t_uint16		wc2;

		// until cch becomes zero or any difference is found.

		while (wc1 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc1, *(pc1 + 1), bByteSwap),
			wc2 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc2, *(pc2 + 1), bByteSwap),
			(--cch) && (wc1) && (wc1 == wc2))
		{
			pc1 += 2; pc2 += 2;
		}

		// return value should mean lexicographic relation between two different characters.
		return ((t_int32)(wc1 - wc2));
	}
}


/*  purpose : compares two UNICODE strings.
input
wcs1    : first string
wcs2    : second string
output
<0 if src <  dst
0  if src == dst
>0 if src >  dst
note
revision history
25-Aug-2004 [Shin, Hee-Sub]: First writing.
07-Sep-2004 [Shin, Hee-Sub]: Byte-alignment.
06-Apr-2005 [Shin, Hee-Sub]:
A critical defect that may cause the comparison to be wrong
when the arguments are not properly aligned was found and fixed.
06-Apr-2005 [Shin, Hee-Sub]:
On the platform that uses big-endian, the result of comparison
between two different strings was incorrect.
*/
t_int32
uni_wcscmp(const t_uint16* wcs1, const t_uint16* wcs2, t_boolean bByteSwap)
{
	t_int32		ret = 0;

	if (!((t_uint)wcs1 & 0x1) && !((t_uint)wcs2 & 0x1)) /* 2-byte aligned */
	{
		// until any different character appears or a terminate-null occurred
		while ((0 == (ret = (t_int32)(*wcs1 - *wcs2))) && *wcs2)
		{
			++wcs1; ++wcs2;
		}
	}
	else /* not-aligned, byte to byte approach - slow */
	{
		t_uint8*	pc1 = (t_uint8 *) wcs1;
		t_uint8*	pc2 = (t_uint8 *) wcs2;
		t_uint16		wc1;
		t_uint16		wc2;

		// until any different character appears or a terminate-null occurred
		while (wc1 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc1, *(pc1 + 1), bByteSwap),
			wc2 = (t_uint16) ESS_UNICODE_MAKEWORD(*pc2, *(pc2 + 1), bByteSwap),
			(0 == (ret = (t_int32)(wc1 - wc2))) && wc2)
		{
			pc1 += 2; pc2 += 2;
		}
	}

	return(ret);
}


/*  purpose : Find a UNICODE character in a UNICODE string.
input
wcs : Null-terminated UNICODE string.
wc  : Unicode character to find.
output
pointer to the first occurrence of wc in wcs.
0 if wc does not occur in wcs.
note
revision history :
25-Aug-2004 [Shin, Hee-Sub]: First writing.
07-Sep-2004 [Shin, Hee-Sub]: Byte-alignment.
06-Apr-2005 [Shin, Hee-Sub]:
A critical defect that may cause the comparison to be wrong
when the arguments are not properly aligned was found and fixed.
06-Apr-2005 [Shin, Hee-Sub]:
On the platform that uses big-endian, the result of comparison
between two different strings was incorrect.
*/
t_uint16 *
uni_wcschr(const t_uint16* wcs, t_uint16 wc, t_boolean bByteSwap)
{
	if (!((t_uint)wcs & 0x1)) // 2-byte aligned
	{
		while ((*wcs) && ((*wcs) != wc))
		{
			wcs++;
		}

		if ((*wcs) == wc)
		{
			return (t_uint16 *) wcs;
		}
	}
	else // not-aligned
	{
		t_int8*		pc = (t_int8 *) wcs;
		t_uint16		wcTemp;

		
		while (wcTemp = (t_uint16) ESS_UNICODE_MAKEWORD(*pc, *(pc + 1), bByteSwap),
			(wcTemp) && (wcTemp != wc))
		{
			pc += 2;
		}

		if (wc == wcTemp)
		{
			return (t_uint16 *) pc;
		}
	}

	return 0;
}


/*  purpose : Concatenate two strings.
input
wcs1 : Destination string.
wcs2 : Pointer to a string to be appended to the destination string.
output
Return the pointer to destination string. (wcs1)
note
revision history :
13-Apr-2005 [Shin, Hee-Sub]: First writing.
*/
t_uint16 *
uni_wcscat(t_uint16* wcs1, const t_uint16* wcs2)
{
	return (t_uint16 *) uni_wcscpy(wcs1 + uni_wcslen(wcs1), wcs2);
}


/*  purpose : Converts an uppercase UNICODE character to a lowercase UNICODE
			  character.
	input
		wc  : an uppercase UNICODE character to convert
	output
		a lowercase UNICODE character if possible.
	note
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_uint16
uni_towlower(
	t_uint16	wc)
{
	if (wc == WEOF)
	{
		return wc;
	}

	if (wc < 256)
	{
		if (!UNI_ISWUPPER(wc))
		{
			return wc;
		}
	}

	return (t_uint16) (wc + casemap_lower[casemap_lower[wc >> 8] + (wc & 0xFF)]);
}


/*  purpose : Converts an lowercase UNICODE character to a uppercase UNICODE
			  character.
	input
		wc  : a lowercase UNICODE character to convert
	output
		an uppercase UNICODE character if possible
	note
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_uint16
uni_towupper(
	t_uint16	wc)
{
	if (wc == WEOF)
	{
		return wc;
	}

	if (wc < 256)
	{
		if (!UNI_ISWLOWER(wc))
		{
			return wc;
		}
	}

	return (t_uint16) (wc + casemap_upper[casemap_upper[wc >> 8] + (wc & 0xFF)]);
}

/*  purpose : uni_isleadbyte returns a nonzero value if the argument is a
			  lead-byte. If the codepage specified by ESS_CODEPAGE is
			  single-byte character set (SBCS), uni_isleadbyte always
			  returns 0.
	input
		c   : an integer value to test
	output
		nonzero - if c is the first byte of multi-byte character.
		0       - if c is not.
	note
		Current codepage affects the result of this function.
	revision history: 
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
			Code was replace to that of MOREX I for speed up the lookup.
*/
t_int32
uni_isleadbyte(
		t_int32	c, 
		t_uint8*	plbIndex)
{
	t_uint8 clb; //lead byte
	t_int32 bLeadByte = 0;

	clb = (t_uint8)(c & 0xff);
	if ((clb < 0x81) || (clb > 0xfe))
	{
		return 0;
	}

	if (cptable->info.codepage == 932)
	{
		if (
			(clb <= 0x9f) ||
			((clb >= 0xe0) && (clb <= 0xfc))
			)
		{
			bLeadByte = 1;
		}			
	}
	else
	{
		if (clb <= 0xfe)
		{
			bLeadByte = 1;
		}
	}

	if (!bLeadByte)
	{
		return 0;
	}

	if (plbIndex != NULL)
	{
		*plbIndex = cptable->dbcs.cp2uni_leadbytes[c & 0xFF];
	}

	return 1;
}

/*  purpose : uni_iswctype test wc for the property specified by the mask
			  argument.
	input
		wc  : an integer value to test
		mask: one of the following values
				C1_UPPER
				C1_LOWER
				C1_DIGIT
				C1_SPACE
				C1_PUNCT
				C1_CNTRL
				C1_BLANK
				C1_XDIGI
				C1_LEADBYTE
				C1_ALPHA
	output
		nonzero - if wc has the property specified by mask.
		0 - if does not.
	note
		there are equivalent wide-char classification routines for each
		property. (e.g. uni_iswupper(), uni_iswlower(), ...)
	revision history
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_int32
uni_iswctype(
	t_uint16	wc, 
	WCTYPE	mask)
{
	t_uint16 ret;

	if (wc == WEOF)
	{
		return 0;
	}

	ret = wctype_table[wctype_table[wc >> 8] + (wc & 0xFF)];

	return (t_int32)(ret & mask);
}


/*  purpose : Converts multi-bytes characters to UNICODE characters
	input :
		wcs
			The address of the wide characters
		mbs
			The address of the multi-bytes characters
		cch
			The number of multi-bytes characters to convert (not bytes)
	output :
		If uni_mbstowcs successfully converts the string, it returns
		the number of converted multi-bytes string.
		If wcs is NULL, it returns the required memory size of mbs.
		  (in this case, cch will be ignored)
		If it encounters a character that cannot be converted,
		it returns -1.
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		01-SEP-2004 [DongYoung Seo]: match function call type
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_uint32
uni_mbstowcs(
	t_uint16* wcs,
	const t_int8* mbs, 
	t_uint32 cch)
{
	if (wcs && (cch == 0))
	{
		return 0;
	}

	// DBCS
	if (cptable->info.char_size > 1)
	{
		if (wcs != NULL)
		{
			return _mbstowcs_dbcs(wcs, mbs, cch);
		}

		return _get_mb_num_dbcs(mbs);
	}
	// SBCS
	else
	{
		if (wcs != NULL)
		{
			return _mbstowcs_sbcs(wcs, mbs, cch);
		}

		return (t_uint32) _get_mb_num_sbcs(mbs);
	}
}


/*  purpose : get character count of multi-bytes characters string
	input :
		mbs
			The address of the multi-bytes characters
	output :
		If it returns the number of converted multi-bytes string.
		If mbs is NULL, it returns 0
		If it encounters a character that cannot be converted,
		it returns -1.
	note :
	revision history :
		01-April-2008 [Soojoeng Kim] First writing.
*/
t_uint32
uni_mbscch(
	const t_int8* mbs)
{
	if (mbs == NULL)
	{
		return 0;
	}

	// DBCS
	if (cptable->info.char_size > 1)
	{
		return _get_mb_num_dbcs(mbs);
	}
	// SBCS
	else
	{
		return (t_uint32) _get_mb_num_sbcs(mbs);
	}
}
/*  purpose : Converts a multi-bytes character to a UNICODE character
	input :
		wc
			The address of the wide characters
		mbc
			The address of the multi-bytes characters
		cch
			The number of bytes to check
	output :
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		01-SEP-2004 [DongYoung Seo]: match function call type
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_int32
uni_mbtowc(
	t_uint16* wc, 
	const t_int8* mbc, 
	t_uint32 cch)
{
	if ((!mbc) || ((*mbc) == 0))
	{
		*wc = 0;
		return 0;
	}

	if (cptable->info.char_size > 1)
	{
		return _mbtowc_dbcs(wc, mbc, cch);
	}
	else
	{
		return _mbtowc_sbcs(wc, mbc, cch);
	}
}


/*  purpose : Converts a multi-bytes character to a UNICODE character
	input :
		wcs
			The address of the wide characters
		mbs
			The address of the multi-bytes characters
		cch
			The number of characters to convert (not bytes)
	output :
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_mbstowcs_dbcs(
	t_uint16* wcs, 
	const t_int8* mbs, 
	t_uint32 cch)
{
	const t_uint16* const	cp2uni = cptable->dbcs.cp2uni;
	t_uint8			clbindex;
	t_uint32			ret;

	for (ret = 0; cch && *mbs; cch--, mbs++, ret++, wcs++)
	{
		if (uni_isleadbyte(*mbs, &clbindex))
		{
			// partial character - ignore it
			if (!(cch - 1))
			{
				return (t_uint32)-1;
			}

			*wcs = cp2uni[(clbindex << 8) + (t_uint8) *(mbs+1)];
			mbs++;  // this is needed only for double-byte character
		}
		else
		{
			*wcs = cp2uni[(t_uint8) *mbs];
		}

		if (*wcs == 0) // invalid character
		{
			// return -1;
			*wcs = '_';
		}

	} // end of for-loop

	// If uni_mbstowcs encounters the null-terminate character either
	// before or when cch occurs, ...
	if (cch && *mbs == 0x0)
	{
		*wcs = 0x0;
	}

	return ret;
}


/*  purpose : Converts a multi-byte character to a UNICODE character
	input :
		wcs
			The address of the wide characters
		mbs
			The address of the multi-byte characters
		cch
			The number of characters to convert (not bytes)
	output :
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
					   Code replaced to that of MOREX I which was more optimized.
*/
static t_uint32
_mbstowcs_sbcs(
	t_uint16* wcs, 
	const t_int8* mbs, 
	t_uint32 cch)
{
	const t_uint16* const cp2uni = cptable->sbcs.cp2uni;
	t_uint16*		dest;
	t_uint16			ret;

	for (dest = wcs; cch && *mbs; cch--)
	{
		ret = cp2uni[(t_uint8) *mbs];
		mbs++;

		// if an invalid character, convert it to a default character
		if (ret == 0)
		{
			// ESS requires '_' as default character (in ESS only)
			// '_' = 0x005F
			ret = 0x005F;
		}

		*dest++ = ret;
	}

	// If uni_mbstowcs encounters the null-terminate character either
	// before or when cch occurs...
	if (cch && (*mbs == 0x0))
	{
		*dest = 0x0;
	}

	return (t_uint32) (dest - wcs);
}


/*  purpose : Converts a multi-byte character to a UNICODE character
	input :
		wc
			The address of the wide characters
		mbc
			The address of the multi-byte characters
		count
			The number of bytes to check
	output :
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_int32
_mbtowc_dbcs(
	t_uint16* wc, 
	const t_int8* mbc, 
	t_uint32 count)
{
	t_uint8	offset = cptable->dbcs.cp2uni_leadbytes[(t_uint8) *mbc];
	const t_uint16*	cp2uni = cptable->dbcs.cp2uni;
	t_uint16			ret;

	// if the first byte of mbc is a lead-byte
	if (offset)
	{
		// if the argument count is not enough or
		// the character that follows the lead-byte is NULL.
		if (count == 1 || *(mbc+1) == 0)
		{
			// ignore it
			return -1;
		}

		// convert the next byte
		ret = cp2uni[(offset << 8) + (t_uint8) *(mbc+1)];
	}
	else
	{
		ret = cp2uni[(t_uint8) *mbc];
	}

	// undefined mapping
	if (ret == 0x0000)
	{
		return -1;
	}

	*wc = ret;

	return offset ? 2 : 1;
}


/*  purpose : Converts a multi-byte character to a UNICODE character
	input :
		wc
			The address of the wide characters
		mbc
			The address of the multi-byte characters
		count
			The number of bytes to check
	output :
	note :
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_int32
_mbtowc_sbcs(
	t_uint16* wc,
	const t_int8* mbc, 
	t_uint32 count)
{
	const t_uint16*	cp2uni = cptable->dbcs.cp2uni;
	t_uint16			ret = cp2uni[(t_uint8) *mbc];

	// undefined mapping
	if (ret == 0x0000)
	{
		return -1;
	}

	*wc = ret;

	count = count;	// suppress compiler warning

	return 1;
}


/*  purpose : Measures the number of characters in a multi-byte string.
	input :
		mbs
			The address of the multi-byte characters
	output :
		The number of characters in a multi-byte
		If mbs has an invalid character, _get_cch_dbcs returns -1.
	note :
		The result may differ from the strlen() in DBCS.
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_get_mb_num_dbcs(
	const t_int8* mbs)
{
	const t_uint8* const cp2uni_leadbytes = cptable->dbcs.cp2uni_leadbytes;
	t_uint32	length;

	if (!*mbs)
	{
		return 0;
	}

	for (length = 0; *mbs; length++)
	{
		// if invalid multi-byte character
		if (_is_invalid_mbc_dbcs(mbs, 2))
		{
			return (t_uint32)-1;
		}

		if (cp2uni_leadbytes[(t_uint8) *mbs++])
		{
			mbs++;
		}
	}

	// we should not count the length of terminating NULL.
	//return length - 1;
	// @20080401-iris : terminating NULL is not counting!
	return length;
}

/*  purpose : Check if mbc is valid.
	input :
		mbc
			The address of the multi-byte character
		count
			The number of characters to convert
	output :
		0   mbc is valid
		-1  mbc is invalid
	note :
		Current codepage affects the result of this function.
	revision history :
		25-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_is_invalid_mbc_dbcs(
	const t_int8* mbc, 
	t_uint32 count)
{
	const t_uint16* const cp2uni = cptable->dbcs.cp2uni;
	t_uint8 offset = cptable->dbcs.cp2uni_leadbytes[(t_uint8) *mbc];

	if (!*mbc)
	{
		return 0;
	}

	if (offset)
	{
		if (mbc[1] == 0x0)
		{
			return (t_uint32)-1;
		}
		if (count == 1)
		{
			return (t_uint32)-1; // partial character
		}

		if (cp2uni[(offset << 8) + (t_uint8) mbc[1]] == 0x0)
		{
			return (t_uint32)-1; // undefined mapping
		}
	}
	else
	{
		if (cp2uni[(t_uint8) *mbc] == 0x0)
		{
			return (t_uint32)-1;
		}
	}

	return 0;
}


static t_uint32
_get_mb_num_sbcs(
	const t_int8* mbs)
{
	const t_uint16* const cp2uni = cptable->sbcs.cp2uni;
	t_uint32 length;

	if (!*mbs)
	{
		return 0;
	}

	for (length = 0; *mbs; length++)
	{
		// if invalid multi-byte character
		if (cp2uni[(t_uint8) *mbs] == 0x0)
		{
			return (t_uint32)-1;
		}

		mbs++;
	}

	// we should not count the length of terminating NULL.
	//return length - 1;	
	// @20080401-iris : terminating NULL is not counting!
	return length;	
}


/*  purpose : Converts UNICODE characters to multi-byte(OEM) characters.
	input :
		mbs
			The address of the multi-byte characters
		wcs
			The address of the wide characters
		count
			The number of byte that can be stored in mbs
	output :
		If uni_wcstombs successfully converts the string, it returns
		the number of bytes written into mbs, excluding the terminating
		NULL(if any).
		If mbs is NULL, uni_wcstombs returns the required size of the
		destination string in bytes excluding terminating NULL.
		If uni_wcstombs encounters a UNICODE character it cannot be
		convert to a multi-byte character, it returns (t_uint32)-1.
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
*/
t_uint32
uni_wcstombs(
	t_int8*	mbs, 
	const t_uint16*	wcs, 
	t_uint32	count)
{
// debug begin
//	NSD_AS(wcs);
//	NSD_AS(cptable);
//	ESS_ASSERTP(wcs != NULL, "source input is null");
//	ESS_ASSERTP(cptable != NULL, "codepage not initialized yet");	// check if codepage initialized normally;
// debug end

	// check exceptional condition
	if (mbs)
	{
		if (count == 0)
		{
			// we have nothing to do, if count is zero.
			return (t_uint32) 0;
		}

		if (!(*wcs))
		{
			// if the source string is L'\0'.
			*mbs = 0;
			return 0;
		}
	}

	// DBCS
	if (cptable->info.char_size > 1)
	{
		// try to locate code that is called more frequently at the first
		if (mbs != 0)
		{
			// do actual conversion
			return _wcstombs_dbcs(mbs, wcs, count);
		}

		// calculate the size of destination buffer required to convert
		// entire string. in this case, the parameter count is ignored.
		return _get_wc_num_dbcs(wcs);
	}
	// SBCS
	else
	{
		// try to locate code that is called more frequently at the first
		if (mbs != 0)
		{
			// do actual conversion
			return _wcstombs_sbcs(mbs, wcs, count);
		}

		// calculate the size of destination buffer required to convert
		// entire string. in this case, the parameter count is ignored.
		return _get_wc_num_sbcs(wcs);
	}
}

/*  purpose : Converts a UNICODE character to a multi-byte character
	input :
		mbc
			The address of the multi-byte characters
		wc
			The UNICODE character to convert
	output :
		If
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
*/
t_int32
uni_wctomb(
	t_int8*	mbc, 
	t_uint16			wc)
{
// debug begin
//	NSD_AS(mbc !=0);
//	ESS_ASSERT(mbc != 0);
// debug end

	// if the destination buffer is NULL, just return zero.
	if (!mbc)
	{
		return 0;
	}

	if (!wc)
	{
		*mbc = 0;
		return 1;
	}

	// ESS requires '_' as default character
	// '_' = 0x5F
	*mbc = 0x5F;

	if (cptable->info.char_size > 1) /* dbcs */
	{
		return _wctomb_dbcs(mbc, wc);
	}
	else
	{
		return _wctomb_sbcs(mbc, wc);
	}
}


/*  purpose : Internal use for converting a UNICODE string to a multi-byte
			  string when the current codepage is DBCS.
	input :
		mbs
			The address of the multi-byte characters
		wcs
			The address of the UNICODE characters
		count
			The number of byte that can be stored in mbs
	output :
		The number of bytes written into mbs, excluding the terminating
		NULL(if any).
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		12-JAN-2005 [Kwon, Moon-Sang]: add type casting for depress warning
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_wcstombs_dbcs(
	t_int8*	mbs,
	const t_uint16*	wcs, 
	t_uint32	count)
{
	const t_uint16* const uni2cp_low  = cptable->dbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->dbcs.uni2cp_high;

	t_uint16	res;
	t_int8*		dest;

	for (dest = mbs; count && *wcs; count--, wcs++)
	{
		res = uni2cp_low[uni2cp_high[(*wcs) >> 8] + ((*wcs) & 0xFF)];

		if (res == 0x0)
		{
			// ESS requires '_' as default character
			// '_' = 0x5F
			*dest++ = (t_int8) 0x5F;
		}
		else
		{
			// if high word of res is a lead-byte
			if (res & 0xFF00)
			{
				// partial character
				// +-----------+----------+
				// | Lead-byte |   NULL   |
				// +-----------+----------+
				if (!--count)
				{
					break;
				}

				// don't worry! endian-neutral
				*dest++ = (t_int8)(res >> 8);
			}
			*dest++ = (t_int8) res;
		}
	}

	// if _wcstombs_dbcs encounters the NULL either before or when count occurs,
	// destination buffer will be null-terminated.
	if (count && ((*wcs) == 0))
	{
		*dest = 0;
	}

	// _wcstombs_dbcs must returns the number of bytes written into the mbs.
	return (t_uint32) (dest - mbs);
}


/*  purpose : Internal use for converting a UNICODE string to a multi-byte
			  string when the current codepage is SBCS.
	input :
		mbs
			The address of the multi-byte characters
		wcs
			The address of the UNICODE characters
		count
			The number of byte that can be stored in mbs
	output :
		The number of bytes written into mbs, excluding the terminating
		NULL(if any).
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		12-JAN-2005 [Kwon, Moon-Sang]: add type casting for depress warning
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_wcstombs_sbcs(
	t_int8* mbs, 
	const t_uint16* wcs, 
	t_uint32 count)
{
	const t_uint8* const uni2cp_low  = cptable->sbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->sbcs.uni2cp_high;

	t_uint8	res;
	t_uint8*	dest;

	for (dest = (t_uint8 *) mbs; count && *wcs; count--, wcs++)
	{
		res = uni2cp_low[uni2cp_high[(*wcs) >> 8] + ((*wcs) & 0xFF)];

		// if undefined mapping
		if (res == 0x0)
		{
			// ESS requires '_' as default character
			// '_' = 0x5F
			*dest++ = (t_int8) 0x5F;
		}
		else
		{
			*dest++ = res;
		}
	}

	// If _wcstombs_sbcs encounters the NULL either before or when count occurs,
	// destination buffer will be null-terminated.
	if (count && ((*wcs) == 0))
	{
		*dest = 0;
	}

	// _wcstombs_sbcs must returns the number of bytes written into the mbs.
	return (t_uint32) (dest - (t_uint8 *) mbs);
}


/*  purpose : Internal use for converting a UNICODE character to a multi-byte
			  character when the current codepage is DBCS.
	input :
		mbc
			The address of the multi-byte character
		wc
			The address of the UNICODE character
	output :
		The number of bytes written into mbc
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_int32
_wctomb_dbcs(
	t_int8*	mbc, 
	t_uint16			wc)
{
	const t_uint16* const uni2cp_low  = cptable->dbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->dbcs.uni2cp_high;

	t_uint16 res = uni2cp_low[uni2cp_high[(wc >> 8)] + (wc & 0xff)];

	// if undefined mapping
	if (res == 0x0000)
	{
		return -1;
	}
	// high word of res is a lead-byte and it consumes 2 bytes.
	else if (res & 0xFF00)
	{
		// don't worry! following codes are endian-neutral.
		*mbc++ = (t_int8)(res >> 8);
		*mbc = (t_int8) res;
		return 2;
	}

	*mbc = (t_int8) res;
	return 1;
}


/*  purpose : Internal use for converting a UNICODE character to a multi-byte
			  character when the current codepage is SBCS.
	input :
		mbc
			The address of the multi-byte character
		wc
			The address of the UNICODE character
	output :
		The number of bytes written into mbc
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_int32
_wctomb_sbcs(
	t_int8*	mbc, 
	t_uint16			wc)
{
	const t_uint8* const uni2cp_low = cptable->sbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->sbcs.uni2cp_high;

	t_uint8 res = uni2cp_low[uni2cp_high[wc >> 8] + (wc & 0xff)];

	// if undefined mapping
	if (res == 0x00)
	{
		return -1;
	}

	*mbc = res;
	return 1;
}


/*  purpose : Measures the number of bytes that is needed when wcs is
			  converted to a multibytes string(DBCS).
	input :
		wcs
			The UNICODE string
		count
			The maximum number of characters to process.
	output :
		The number of bytes that is needed to convert.
		If _get_cch_dbcs encounters a character that cannot be converted,
		it returns -1.
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_get_wc_num_dbcs(
	const t_uint16*	wcs)
{
	const t_uint16* const uni2cp_low = cptable->dbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->dbcs.uni2cp_high;

	t_uint16			res;
	t_uint32	length;

	if (!*wcs)
	{
		return 0;
	}

	// until it encounters the null (end of string)
	for (length = 0; *wcs; wcs++, length++)
	{
		res = uni2cp_low[uni2cp_high[(*wcs) >> 8] + ((*wcs) & 0xFF)];
		if (res == 0x0000)
		{
			// wcs contains wrong characters that cannot be converted.
			return (t_uint32)-1;
		}

		// if the high word of res is a lead-byte, it consumes 2 bytes.
		if (res & 0xFF00)
		{
			length++;
		}
	}

	// length should not count the space for the terminating NULL.
	return length - 1;
}


/*  purpose : Measures the number of bytes that is needed when wcs is
			  converted to a multi-byte string(SBCS).
	input :
		wcs
			The UNICODE string
	output :
		The number of bytes that is needed to convert.
		If _get_cch_sbcs encounters a character that cannot be converted,
		it returns -1.
	note :
	revision history :
		31-Aug-2004 [Shin, Hee-Sub]: First writing.
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
static t_uint32
_get_wc_num_sbcs(
	const t_uint16*	wcs)
{
	const t_uint8* const uni2cp_low = cptable->sbcs.uni2cp_low;
	const t_uint16* const uni2cp_high = cptable->sbcs.uni2cp_high;

	t_uint32 length;

	if (!*wcs)
	{
		return 0;
	}

	// until it encounters the null(end of string)
	for (length = 0; *wcs; wcs++, length++)
	{
		if (uni2cp_low[uni2cp_high[(*wcs) >> 8] + ((*wcs) & 0xFF)] == 0x00)
		{
			// wcs contains wrong characters that cannot be converted.
			return (t_uint32)-1;
		}
	}

	// length should not count the space for the terminating NULL.
	return length - 1;
}




/*  purpose : uni_tolower converts the argument to a lowercase letter.
	input
		c   : a character to convert.
	output
		uppercase letter of the argument c if possible.
	note
		If there is no appropriate character, uni_tolower just returns c.
		Current codepage affects the result of this function.
	revision history
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_int32
uni_tolower(
	t_int32	c,
	t_boolean bByteSwap)
{
	t_uint16		wc;
	t_int32			res = 0;
	t_int8		buffer[4];

	if (uni_isleadbyte((c >> 8) & 0xff, NULL))
	{
		buffer[0] = (t_int8) ((c >> 8) & 0xFF);
		buffer[1] = (t_int8) (c & 0xFF);
		buffer[2] = 0;
		if (uni_mbtowc(&wc, buffer, 2) == -1)
		{
			return c;
		}
	}
	else
	{
		buffer[0] = (t_int8)(c & 0xFF);
		buffer[1] = 0;
		if (uni_mbtowc(&wc, buffer, 1) == -1)
		{
			return c;
		}
	}
	wc = uni_towlower(wc);

	uni_wctomb((t_int8 *) &res, wc);

	if (bByteSwap)
	{
		return (t_int32) BYTE_SWAP_32BIT(res);
	}
	else
	{
		return (t_int32)res;
	}
	
	
}

/*  purpose : uni_toupper converts the argument to a uppercase letter.
	input
		c   : a character to convert.
	output
		lowercase letter of the argument c if possible.
	note
		If there is no appropriate character, uni_toupper just returns c.
		Current codepage affects the result of this function.
	revision history
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
*/
t_int32
uni_toupper(
	t_int32	c,	
	t_boolean bByteSwap)
{
	t_uint16		wc;
	t_int32			res = 0;
	t_int8		buffer[4];

	if (uni_isleadbyte((c >> 8) & 0xff, NULL))
	{
		buffer[0] = (t_int8)((c >> 8) & 0xFF);
		buffer[1] = (t_int8)(c & 0xFF);
		buffer[2] = 0;
		if (uni_mbtowc(&wc, buffer, 2) == -1)
		{
			return c;
		}
	}
	else
	{
		buffer[0] = (t_int8)(c & 0xFF);
		buffer[1] = 0;
		if (uni_mbtowc(&wc, buffer, 1) == -1)
		{
			return c;
		}
	}

	wc = uni_towupper(wc);

	uni_wctomb((t_int8 *)&res, wc);

	if (bByteSwap)
	{
		return (t_int32) BYTE_SWAP_32BIT(res);
	}
	else
	{
		return (t_int32)res;
	}
}


#if 1
/*  purpose : change to upper case string
	input :
		pStr : string pointer
		dwLength : string length
	output :
		none
	note :
	revision history :
		27-JUL-2004 [DongYoung Seo] : Unicode Adaptation
		25-MAR-2006 [Lee, Chung-Shik]: Modified for dynamic codepage support.
						Part of code was replaced to that of MOREX I for efficiency.
*/
void
uni_toupper_str(
	t_uint8*	pStr, 
	t_uint32	dwLength,
	t_boolean	bByteSwap)
{
	t_uint32	i;

//	ESS_ASSERT(pStr != NULL);

	for (i = 0; i < dwLength; )
	{
		if (uni_isleadbyte(*pStr, NULL) == 1)
		{
			i += 2;
			pStr += 2;
		} 
		else
		{
			*pStr = (t_uint8)uni_toupper(*pStr, bByteSwap);
			pStr++;
			i++;
		}
	} // end of for
}


/*  purpose : change to lower case string
	input :
		pStr : string pointer
		dwLength : string length
	output :
		none
	note :
	revision history :
		27-JUL-2004 [DongYoung Seo] : Unicode Adaptation
		25-MAR-2006 [Lee, Chung-Shik]: Dynamic codepage support.
						Part of code was replaced to that of MOREX I for efficiency.
*/
void
uni_tolower_str(
	t_uint8*	pStr,
	t_uint32	dwLength,
	t_boolean	bByteSwap)
{
	t_uint32 i;

	for (i = 0; i < dwLength; )
	{
		if (uni_isleadbyte(*pStr, NULL) == 1)
		{
			i += 2;
			pStr += 2;
		}
		else
		{
			*pStr = (t_uint8)uni_tolower(*pStr, bByteSwap);
			pStr++;
			i++;
		}
	} // end of for
}


/*  purpose : change to upper case string
	input :
		pStr : string pointer
		dwLength : string length
	output :
		none
	note :
	revision history :
		27-JUL-2004 [DongYoung Seo] : Unicode Adaptation
*/
void
uni_towupper_str(
	t_uint16*	pStr, 
	t_uint32	dwLength)
{
	t_uint32 i;

//	ESS_ASSERT(pStr != NULL);

	for (i = 0; i < dwLength; i++)
	{
		*pStr = uni_towupper(*pStr);
	}
}

/*  purpose : change to lower case string
	input :
		pStr : string pointer
		dwLength : string length
	output :
		none
	note :
	revision history :
		27-JUL-2004 [DongYoung Seo] : Unicode Adaptation
*/
void
uni_towlower_str(
	t_uint16*	pStr,
	t_uint32	dwLength)
{
	t_uint32 i;

//	ESS_ASSERT(pStr != NULL);

	for (i = 0; i < dwLength; i++)
	{
		*pStr = uni_towlower(*pStr);
	}

}

#endif


/*  purpose : convert wchar string to number
	input :
		t_uint16 *pwsz : string pointer to convert
	output :
		return : converted number
	note:
	revision history :
		30-SEP-2005 [Shin, Hee-Sub]: First writing.
*/
t_int32
uni_wtoi(
	const t_uint16* pwsz)
{
	t_int32		c;
	t_int32		sign;
	t_int32		total = 0;

	while (UNI_ISWSPACE(*pwsz))
	{
		pwsz++;
	}

	sign = c = (t_int32) *pwsz++;
	if (c == '-' || c == '+')
	{
		c = (t_int32) *pwsz++;
	}

	while ((c = (t_int32)_wchartodigit((t_uint16)c)) != -1)
	{
		total = 10 * total + c;
		c = *pwsz++;
	}

	return (sign == '-' ? -total : total);
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
uni_itoa(
	t_int32 nVal, 
	t_int8* str)
{
	t_int32		bMinus;
	t_int32		nMaxColumn = 1000000000;
	t_int32		nMaxCol = 11;

//	ESS_ASSERTP(str != NULL,"invalid parameter");

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

/*  purpose : convert number to wchar string
	input :
		int value   : source number to convert
	output:
		t_uint16 *pwsz : target wchar string pointer
	note :
	revision history :
		30-SEP-2005 [Shin, Hee-Sub]: First writing.
*/
#define INT_MAX_CCH 40

t_uint16*
uni_itow(
	t_int32			value, 
	t_uint16*	pwsz)
{
	t_int8 temp[INT_MAX_CCH];

	uni_itoa(value, temp);
	uni_mbstowcs(pwsz, temp, INT_MAX_CCH);

	return pwsz;
}

/*  purpose : convert t_uint16 character to digit
	input :
		t_uint16 ch : source wchar character to convert
	 output :
		return : converted digit value
	note :
	revision history :
		30-SEP-2005 [Shin, Hee-Sub]: First writing.
*/
static t_int32
_wchartodigit(
	t_uint16	ch)
{
	#define DIGIT_TEST(zero) \
	if (ch < zero) \
		return -1; \
	if (ch < zero + 10) \
		return ch - zero

	DIGIT_TEST(0x0030);
	DIGIT_TEST(0x0660);
	DIGIT_TEST(0x06F0);
	DIGIT_TEST(0x0966);
	DIGIT_TEST(0x09E6);
	DIGIT_TEST(0x0A66);
	DIGIT_TEST(0x0AE6);
	DIGIT_TEST(0x0B66);
	DIGIT_TEST(0x0C66);
	DIGIT_TEST(0x0CE6);
	DIGIT_TEST(0x0D66);
	DIGIT_TEST(0x0E50);
	DIGIT_TEST(0x0ED0);
	DIGIT_TEST(0x0F20);
	DIGIT_TEST(0x1040);
	DIGIT_TEST(0x17E0);
	DIGIT_TEST(0x1810);
	DIGIT_TEST(0xFF10);

#undef DIGIT_TEST

	return -1;
}


