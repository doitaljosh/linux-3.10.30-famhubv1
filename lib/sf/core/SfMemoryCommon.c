/**
****************************************************************************************************
* @file SfMemoryCommon.c
* @brief Security framework [SF] functions for working with memory
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 7, 2014 9:40
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

// local 
#include <uapi/linux/sf/core/SfMemory.h>
#include <uapi/linux/sf/core/SfDebug.h>
#if defined(SF_LEVEL_USER)
    #include <string.h>
#endif // !SF_LEVEL_USER

// system 
#ifdef SF_LEVEL_USER
#include <memory.h>
#endif /* SF_LEVEL_USER */

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline void* SFAPI sf_memset(void* pArray, Int value, Uint32 number)
{
    SF_ASSERT(SF_DEBUG_CLASS_CORE, pArray != NULL, "Invalid arguments for '%s'", __FUNCTION__);
    return memset(pArray, value, number);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline SFLIB void* SFAPI sf_memcpy(void * pDestination, const void * pSource, size_t size)
{
	SF_ASSERT(SF_DEBUG_CLASS_CORE, ((NULL != pDestination) && (NULL != pSource)),
		"Invalid arguments for '%s'", __FUNCTION__);
	return memcpy(pDestination, pSource, size);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline SFLIB Char* SFAPI sf_strncpy(Char* pDestination, const Char* pSource, size_t size)
{
	SF_ASSERT(SF_DEBUG_CLASS_CORE, ((NULL != pDestination) && (NULL != pSource)),
		"Invalid arguments for '%s'", __FUNCTION__);
	return strncpy(pDestination, pSource, size);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline void* SFAPI sf_zero_memory(void* pArray, Uint32 number)
{
	return sf_memset(pArray, 0, number);
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline void* SFAPI sf_poison_memory(void* pArray, Uint32 number)
{
	Uint32 i = 0;
	Uchar* ptr = pArray;
	for(i = 0; i < number; i++)
	{
		ptr[i] = 0xaf; ///< TODO: Memory randomization
	}

	return pArray;
}
