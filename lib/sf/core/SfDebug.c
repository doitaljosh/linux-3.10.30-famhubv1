/**
****************************************************************************************************
* @file SfDebug.c
* @brief Security framework [SF] debug implementation
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 4, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <uapi/linux/sf/core/SfConfig.h>
#include <uapi/linux/sf/core/SfDebug.h>
#include <uapi/linux/sf/core/SfTime.h>
#include <uapi/linux/sf/core/SfMemory.h>
#include <stdarg.h>
#include <linux/kernel.h>

#if defined(SF_LEVEL_USER)
    #include <string.h>
    #include <stdlib.h>
#endif

/**
****************************************************************************************************
*
****************************************************************************************************
*/
#define BUFFER_SZIE 512

static const Char* DEBUG_LEVEL_ALIASES[] = {"E","W", "I"};

static const Char* DEBUG_CLASS_ALIASES[] = {"???", "COR", "SFD", "UEP", "NTF", "DSP", "CNT", "INC",
    "NLS", "PRT", "TRP", "SFC", "FRWL", "PRMTV", "TST" };

SfDebuggerContext g_debugContext;

/**
****************************************************************************************************
* @brief This struct containt debuger options information
****************************************************************************************************
*/
typedef struct
{
    SF_DEBUG_CLASS debugClass;
    SF_DEBUG_LEVEL debugLevel;
} SfDebugOption;

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SfDebugOption debugTable[] =
{
    {SF_DEBUG_CLASS_UNDEFINED, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_CORE, SF_DEBUG_LEVEL_INFO},
    {SF_DEBUG_CLASS_SFD, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_UEP, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_NTF, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_DSP, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_CNT, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_INTERCONNECT, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_PRT, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_TRP, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_SFC, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_FRWL, SF_DEBUG_LEVEL_MAX},
    {SF_DEBUG_CLASS_PRMTV, SF_DEBUG_LEVEL_MAX},
	{SF_DEBUG_CLASS_TEST, SF_DEBUG_LEVEL_MAX}
};

/**
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfLogHandler(SF_DEBUG_CLASS debugClass, SF_DEBUG_LEVEL debugLevel,
                             const Char* format, ...)
{
    SF_STATUS result = SF_STATUS_OK;
    if (debugLevel <= g_debugContext.debugLevel)
    {
        if (debugClass > SF_DEBUG_CLASS_MAX ||
            debugLevel > sizeof(DEBUG_LEVEL_ALIASES) / sizeof(DEBUG_LEVEL_ALIASES[0]))
        {
            debugClass = SF_DEBUG_CLASS_UNDEFINED;
        }

        if (debugClass <= SF_DEBUG_CLASS_MAX &&
            debugLevel < sizeof(DEBUG_LEVEL_ALIASES) / sizeof(DEBUG_LEVEL_ALIASES[0]))
        {
            Char buffer[ BUFFER_SZIE ] = {0,};
            Char* pLastChar = buffer;
            Char* pBuffer = buffer;
            size_t bufferLeft = sizeof(buffer);
            size_t c_lineSize = 0;
            va_list args;
            size_t len;

            /** Print time header */
            SfTime time;
            SfParseTimeStructure(&time, SfGetSystemTimeUsec() - g_debugContext.startTime);
            len = sf_snprintf(pLastChar, bufferLeft, "[%02d:%02d:%02d:%02d:%03d]: ", time.days,
                time.hours, time.minutes, time.sec, time.msec);
            bufferLeft -= len;
            pLastChar += len;
            /** Print Process Id, and Thread Id */

            /** TODO: Dummy for future usage */
            //getpid gettid

            /** Print message header */
            len = sf_snprintf(pLastChar, bufferLeft, "[%s][%s]: ", DEBUG_LEVEL_ALIASES[debugLevel],
                DEBUG_CLASS_ALIASES[debugClass]);
            bufferLeft -= len;
            pLastChar += len;
            va_start(args, format);

            /** Print message body */
            c_lineSize = strlen(format) + strlen(buffer) + 2; // 1 == '\n'
            if( c_lineSize > BUFFER_SZIE )
            {
                size_t c_bufferSize = 0;
                pBuffer = (Char*)sf_malloc(c_lineSize);
                pLastChar = pBuffer;
                bufferLeft = c_lineSize;
                c_bufferSize = strlen( buffer );
                memcpy( pBuffer, buffer, c_bufferSize );
                bufferLeft -= c_bufferSize;
                pLastChar += c_bufferSize;
            }

#ifdef SF_OS_LINUX
            len = vsnprintf(pLastChar, bufferLeft, format, args);
#elif defined(SF_OS_WINDOWS)
            vsnprintf_s(pLastChar, bufferLeft, bufferLeft, format, args);
#endif
            bufferLeft -= len;
            pLastChar += len;
            va_end(args);

                /** Print tail */
                sf_snprintf(pLastChar, bufferLeft, "\n");
                SfOutputDebugString(&g_debugContext, pBuffer);

            if( c_lineSize > BUFFER_SZIE )
            {
                sf_free(pBuffer);
            }
        }
        else
        {
            result = SF_STATUS_FAIL;
        }
    }
    result = ( (SF_DEBUG_LEVEL_ERROR == debugLevel) && g_debugContext.breakOnAssertion) ?
            SF_STATUS_FAIL : SF_STATUS_OK;

    return result;
}

EXPORT_SYMBOL(SfLogHandler);
