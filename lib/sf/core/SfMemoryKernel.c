/**
****************************************************************************************************
* @file SfMemoryKernel.c
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

#include <uapi/linux/sf/core/SfMemory.h>
#include <uapi/linux/sf/core/SfDebug.h>

#include <linux/slab.h>

/**
****************************************************************************************************
*
****************************************************************************************************
*/
inline void* SFAPI sf_malloc(Uint32 size)
{
#ifdef SF_MEMORY_POISON
    return kmalloc(size, GFP_KERNEL);
#else
    void* pArray = kmalloc(size, GFP_KERNEL);
    SF_ASSERT(SF_DEBUG_CLASS_CORE, pArray != NULL, "NULL pointer '%p'", __FUNCTION__);
    return sf_poison_memory(pArray, size);
#endif	/* !SF_MEMORY_POISON*/
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
inline void SFAPI sf_free(void* pArray)
{
    SF_ASSERT(SF_DEBUG_CLASS_CORE, pArray != NULL, "Invalid arguments for '%s'", __FUNCTION__);
    kfree(pArray);
}
