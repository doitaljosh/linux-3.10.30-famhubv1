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
 * @file		ess_debug.c
 * @brief		Base modue for FFAT debug
 * @author		DongYoung Seo(dy76.seo@samsung.com)
 * @version		JUL-04-2006 [DongYoung Seo] First writing
 * @see			None
 */

#include "ess_types.h"
#include "ess_debug.h"

#if defined(_WIN32) && defined(_MSC_VER) && defined(_DEBUG)
	// disable warning C4996 -- for deprecated function
	#pragma warning(disable	: 4996)
#endif

t_int32	(*pfnEssPrintf)(char* pFmt, ...)			= ESS_DebugNullPrintf;
t_int32	(*pfnEssGetChar)(void)						= ESS_DebugNullGetChar;

/**
 * initialize debug module
 *
 * @param		pfnPrintf	: print function pointer
 * @param		pfnGetChar	: character input function pointer
 * @author		DongYoung Seo
 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
 */
void
ESS_DebugInit(t_int32 (*pfnPrintf)(const char* pFmt,...), t_int32 (*pfnGetChar)(void))
{
	if (pfnPrintf != NULL)
	{
		pfnEssPrintf = (t_int32	(*)(char* pFmt, ...))pfnPrintf;
	}

	if (pfnGetChar != NULL)
	{
		pfnEssGetChar = pfnGetChar;
	}

	return;
}


#ifdef ESS_DEBUG
	/**
	 * print assert message
	 *
	 * @param		psFileName	: file name
	 * @param		dwLine		: line number
	 * @param		pszMsg		: message to print
	 * @param		strFuncName : function name
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	void
	ESS_Assert(const char* psFileName, t_int32 dwLine,
				char* pszMsg, const char* strFuncName)
	{
		pfnEssPrintf("\n%s(%s, %d) : %s\n\n",
				strFuncName,
				ESS_PathStart(psFileName),
				dwLine,
				pszMsg);
#ifdef _WIN32
		__debugbreak();
#else
		{
			t_int32			i;

			pfnEssPrintf("quit or continue? (q/c) : ");
			i = pfnEssGetChar();
			if (i != 'q')
			{
				return;
			}
			ESS_ABORT();
		}
#endif
	}


	/*	purpose : print a formatted string
		input :
			pFmt : print format string
			... : parameters
		output :
			number of chars on success
			< 0 on failure
		note :
		revision history :
	*/
	t_int32
	ESS_Printf(char* pFmt, ...)
	{
		static t_int8	_pBuff[2048];
		va_list		ap;

		va_start(ap, pFmt);
		vsprintf((char*)_pBuff, (const char*)pFmt, ap);
		va_end(ap);

		return pfnEssPrintf((char*)_pBuff);
	}


	/**
	 * print buffers in hex
	 *
	 * @param		pBuff		: buffer pointer of one sector
	 * @param		dwLength	: length
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	void
	ESS_DumpBuffer(t_int8* pBuff, t_int32 dwLength)
	{
		t_int32		i, j;

		pfnEssPrintf("___");
		for (i = 0; i < 16; i++)
		{
			pfnEssPrintf("____");
		}
		pfnEssPrintf("\n");

		pfnEssPrintf("   ");
		for (i = 0; i < 16; i++)
		{
			pfnEssPrintf(" %02X ", i);
		}

		do
		{
			for (i = 0; i < 32; i++)
			{
				pfnEssPrintf("\n");
				pfnEssPrintf("%04X", i);
				for (j = 0; j < 16; j++)
				{
					pfnEssPrintf(" %02X ", *pBuff++);
					dwLength--;
					if ((t_int32)dwLength <= 0)
					{
						goto out;
					}
				}
			}
	out:
			pfnEssPrintf("\n");
			pfnEssPrintf("___");
			for (i = 0; i < 16; i++)
			{
				pfnEssPrintf("____");
			}
			pfnEssPrintf("\n");

		} while (dwLength > 0);
		pfnEssPrintf("\n");
	}

	#define ESS_ASSERT_IFS		'\\'
	#define ESS_ASSERT_DEPTH	2


	/**
	 * get path component
	 *
	 * @param		psFileName	: full path string
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	char*
	ESS_PathStart(const char* psFileName)
	{
		t_uint8 		dwCount;
		size_t			dwLength;
		char*			cp;

		dwLength = ESS_STRLEN(psFileName);

		cp = (char*)(psFileName + dwLength - 1);

		dwCount = 0;
		while (cp != psFileName)
		{
			if ((*cp) == ESS_ASSERT_IFS)
			{
				dwCount++;
				if (dwCount == ESS_ASSERT_DEPTH)
				{
					break;
				}
			}
			cp--;
		}
		return cp;
	}
#endif	// endof #ifdef ESS_DEBUG

// stack check routines
//
#if defined(ESS_STACK_CHECK)
	static t_uint32		dwStackDepth = 0;
	static t_uint32		dwStackStartTmp;
	static t_uint32		dwStackEndTmp;

	static char		szStackStartMsg[100];
	static char		szStackEndMsg[100];
	static char		szStackStartMsgTmp[100];
	static char		szStackEndMsgTmp[100];

	/**
	 * setup start stack address
	 *
	 * @param		pdwStartAddress : stack start address
	 * @param		pszMsg : message (function name)
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	void
	ESS_StackStart(void *pdwStartAddress, const char *pszMsg)
	{
		dwStackStartTmp = (t_uint32) pdwStartAddress;
		strncpy(szStackStartMsgTmp, pszMsg, 99);

	#ifdef ESS_STACK_CHECK_EX
		ESS_StackStartEx(pdwStartAddress, pszMsg);
	#endif
	}

	/**
	 * setup end stack address
	 *
	 * @param		pdwEndAddress : stack end address
	 * @param		pszMsg : message (function name)
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	void
	ESS_StackEnd(void *pdwEndAddress, const char *pszMsg)
	{
	#ifdef TFS4_STACK_CHECK_EX
		ESS_StackEndEx(pdwEndAddress, pszMsg);
	#endif

		dwStackEndTmp = (t_uint32) pdwEndAddress;
		strncpy(szStackEndMsgTmp, pszMsg, 99);

		if (dwStackDepth <= (dwStackStartTmp - dwStackEndTmp + 1))
		{
			if (dwStackDepth == (dwStackStartTmp - dwStackEndTmp + 1))
			{
				if (strcmp(szStackStartMsg, szStackStartMsgTmp) == 0 &&
					strcmp(szStackEndMsg, szStackEndMsgTmp) == 0)
				{
					return;
				}
			}
			else
			{
				dwStackDepth = (dwStackStartTmp - dwStackEndTmp + 1);
				strcpy(szStackStartMsg, szStackStartMsgTmp);
				strcpy(szStackEndMsg, szStackEndMsgTmp);
				pfnEssPrintf("New stack depth! : %s -- %s: %5d\n",
							szStackStartMsgTmp, szStackEndMsgTmp, dwStackDepth);

#if !defined(_WIN32) && !defined(__GNUC__)
				if (dwStackDepth > (1024 * 5))
				{
					pfnEssPrintf("Stack usage is over stack limit 5KB \n");

					pfnEssPrintf("quit or continue? (q/c) : ");
					if (pfnEssGetChar() != 'q')
					{
						return;
					}

					ESS_ABORT();
				}
#endif
			}
		}
	}

	/**
	 * print stack state
	 *
	 * @param		None
	 * @author		DongYoung Seo
	 * @version		JUL-04-2006 [DongYoung Seo] First Writing.
	 */
	void
	ESS_StackShow(void)
	{
		pfnEssPrintf("Maximum stack depth = %5d, ", dwStackDepth);
		pfnEssPrintf("= 0x%05X\n", dwStackDepth);
		pfnEssPrintf("%s -- %s\n", szStackStartMsg, szStackEndMsg);

	#ifdef TFS4_STACK_CHECK_EX
		ESS_StackShowEx();
		pfnEssPrintf("Clear all stack usage info. of all functions?");
		if (EssOsal_GetChar() == 'y')
		{
			_tfs4_stack_clean_ex();
		}
	#endif
	}

#endif /* defined(ESS_STACK_CHECK) */


#ifdef ESS_DEBUG_PRT

	static t_uint32 dwSteps = 0;

	/*  purpose : print log message
		input :
			msg : message to print
			strFileName : file name
			nLineNumber : line number
		output :
			none
		note :
		revision history :
	*/
	void
	ESS_PrintLog(const char* msg, const char* strFileName,
					t_int32 nLineNumber, const char* strFuncName)
	{
		pfnEssPrintf("-------------------------------------------------------------------");
		pfnEssPrintf("%04d %s(%s, %d)\n -> %s",
					dwSteps++, strFuncName,
					ESS_PathStart(strFileName), nLineNumber, msg);
	}

	/*  purpose : return log message
		input :
			msg : message to print
			strFileName : file name
			nLineNumber : line number
		output :
			log buffer
		note :
		revision history :
	*/
	const t_int8*
	ESS_Src(const char* msg, const char* psFileName,
					t_int32 nLineNumber, const char* strFuncName)
	{
		static char buff[1024];

		ESS_SPRINTF(buff, "%s(%s, %d) -> %s: ",
						ESS_PathStart(psFileName), strFuncName, nLineNumber, msg);

		return (t_int8*)buff;
	}

#endif	// end of #ifdef ESS_DEBUG_PRT


/*	purpose : print a string to NULL 
	input :
		fmt : print format string
		... : parameters
	output :
		number of chars on success
		< 0 on failure
	note :
	revision history :
*/
t_int32
ESS_DebugNullPrintf(char* pFmt, ...)
{
	pFmt = pFmt;

	// do nothing.
	return ESS_TRUE;
}


t_int32
ESS_DebugNullGetChar(void)
{
	return ESS_TRUE;
}


