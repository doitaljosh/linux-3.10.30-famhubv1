/**
****************************************************************************************************
* @file SfDebugLinux.c
* @brief Security framework [SF] debug specialiation for Linux
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 4, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/
#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfMemory.h>
#include <uapi/linux/sf/core/SfTime.h>
#include <linux/kernel.h>

#if defined(SF_LEVEL_USER)
    #if defined(__arm__)
        #define LOG_TAG "org.tizen.smart_security"
        #include <dlog.h>
    #endif  // __arm__
#endif  // SF_LEVEL_USER

/**
****************************************************************************************************
*
****************************************************************************************************
*/
extern SfDebuggerContext g_debugContext;

#ifdef SF_LEVEL_USER
/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfOpenDebuggerContext(SfDebuggerContext* pDebuggerContext)
{
    SF_STATUS status = SF_STATUS_FAIL;
    if (NULL == pDebuggerContext)
    {
        pDebuggerContext = &g_debugContext;
    }
    sf_memset(pDebuggerContext, 0, sizeof(SfDebuggerContext));
    pDebuggerContext->header.size = sizeof(SfDebuggerContext);
    pDebuggerContext->header.version = SF_CORE_VERSION;
    pDebuggerContext->pOutputStream = stderr;

    if (NULL != pDebuggerContext->pOutputStream)
    {
        pDebuggerContext->breakOnAssertion = TRUE;
        pDebuggerContext->debugLevel = SF_DEBUG_LEVEL_MAX;
        pDebuggerContext->startTime = SfGetSystemTimeUsec();
        status = SF_STATUS_OK;
    }

    return status;
}
#else
/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfOpenDebuggerContext(SfDebuggerContext* pDebuggerContext)
{
    SF_STATUS status = SF_STATUS_FAIL;
    if (NULL == pDebuggerContext)
    {
        pDebuggerContext = &g_debugContext;
    }
    sf_memset(pDebuggerContext, 0, sizeof(SfDebuggerContext));
    pDebuggerContext->header.size = sizeof(SfDebuggerContext);
    pDebuggerContext->header.version = SF_CORE_VERSION;

    pDebuggerContext->breakOnAssertion = TRUE;
    pDebuggerContext->debugLevel = SF_DEBUG_LEVEL_MAX;
    pDebuggerContext->startTime = SfGetSystemTimeUsec();
    status = SF_STATUS_OK;

    return status;
}
#endif

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfCloseDebuggerContext(SfDebuggerContext* pDebuggerContext)
{
    if (NULL == pDebuggerContext)
    {
        return SF_STATUS_BAD_ARG;
    }

    pDebuggerContext->header.size = 0;
    return SF_STATUS_OK;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SFAPI SfOutputDebugString(const SfDebuggerContext* pDebuggerContext, const Char* pMessage)
{
    if ( (NULL != pDebuggerContext) && (NULL != pMessage) )
    {
#if defined(SF_LEVEL_USER)
        fprintf(pDebuggerContext->pOutputStream, "%s", pMessage);
#if defined(__arm__)
        LOGE( "%s", pMessage );
#endif  // __arm__
#elif defined(SF_LEVEL_KERNEL)
        printk(KERN_INFO"%s", pMessage);
#else
#error Unknown output method. Please, refer to 'SfDebugLinux.c' file.
#endif /* !SF_LEVEL_USER & !SF_LEVEL_KERNEL */
    }
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SFAPI SfDebugBreak(void)
{
    __builtin_trap();
}
