/**
****************************************************************************************************
* @file SfValidator.c
* @brief Security framework [SF] common validation routines
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Mar 7, 2014 13:27
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfValidator.h>
#include <uapi/linux/sf/core/SfVersion.h>

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline Bool SFAPI SfIsContextValid(const SfContextHeader* pContextHeader, Uint32 size)
{
	return (NULL != pContextHeader && pContextHeader->size == size) ? TRUE : FALSE;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
inline Bool SFAPI SfIsContextInitialized(const SfContextHeader* pContextHeader, Uint32 size)
{
	return (SfIsContextValid(pContextHeader, size) &&
		pContextHeader->state > SF_CONTEXT_STATE_UNINITIALIZED) ? TRUE : FALSE;
}
