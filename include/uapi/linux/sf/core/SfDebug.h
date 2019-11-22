/**
****************************************************************************************************
* @file SfDebug.h
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

#ifndef _SF_DEBUG_H_
#define _SF_DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

/**
****************************************************************************************************
*
****************************************************************************************************
*/
#include "SfConfig.h"
#include "SfTypes.h"
#include "SfStatus.h"

#ifdef SF_LEVEL_USER
#include <stdio.h>
#endif

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

/**
****************************************************************************************************
* @brief Macros
****************************************************************************************************
*/
#define SF_GET_SYSTEM_ERROR(errno) \
({ \
    const Int bufferSize = 256; \
    Char buffer[bufferSize] = {0}; \
    strerror_r(errno, buffer, sizeof(buffer)); \
})

/**
****************************************************************************************************
* @brief Debug classes enumeration
****************************************************************************************************
*/
typedef enum
{
    /**
    * @warning ATTENTION!: when we change this enumeration we should change DEBUG_CLASS_ALIASES array
    */
    SF_DEBUG_CLASS_UNDEFINED = 0, ///< Undefined class
    SF_DEBUG_CLASS_CORE = 1, ///< Class for SF core debugging
    SF_DEBUG_CLASS_SFD = 2, ///< Class for security framework driver debugging
    SF_DEBUG_CLASS_UEP = 3, ///< Class for Unauthorized Execution prevention module debugging
    SF_DEBUG_CLASS_NTF = 4, ///< Class for Notifier module debugging
    SF_DEBUG_CLASS_DSP = 5, ///< Class for Dispatcher module debugging
    SF_DEBUG_CLASS_CNT = 6, ///< Class for Container module debugging
    SF_DEBUG_CLASS_INTERCONNECT = 7, ///< Class for security framework interconnection debugging
    SF_DEBUG_CLASS_NLS = 8, ///< Class for netlink socket debugging
    SF_DEBUG_CLASS_PRT = 9, ///< Class for protocol library
    SF_DEBUG_CLASS_TRP = 10, ///< Class for transport library
    SF_DEBUG_CLASS_SFC = 11, ///< Security Framework controlling library
    SF_DEBUG_CLASS_FRWL = 12, ///< Class for app_firewall debugging
    SF_DEBUG_CLASS_PRMTV = 13, ///< Class for libprimitive
    SF_DEBUG_CLASS_TEST = 14, ///< Class fir unit test modules
    SF_DEBUG_CLASS_MAX = SF_DEBUG_CLASS_TEST
} SF_DEBUG_CLASS;

/**
****************************************************************************************************
* @brief Debug level enumeration
****************************************************************************************************
*/
typedef enum
{
    /**
    * @warning ATTENTION!: when we change this enumeration we should change DEBUG_LEVEL_ALIASES array
    */
    SF_DEBUG_LEVEL_ERROR = 0, ///< Debug level ERROR
    SF_DEBUG_LEVEL_WARNING = 1, ///< Debug level WARNING
    SF_DEBUG_LEVEL_INFO = 2, ///< Debug level INFORMATION
    SF_DEBUG_LEVEL_MAX = SF_DEBUG_LEVEL_INFO
} SF_DEBUG_LEVEL;

/**
****************************************************************************************************
* @brief This struct containt Debugger context information
****************************************************************************************************
*/
typedef struct
{
    SfContextHeader header; ///< Header of a structure (contains size and version)

#ifdef SF_LEVEL_USER
    FILE* pOutputStream; ///< Stream to output logs
#endif /* !SF_LEVEL_USER */

    Bool breakOnAssertion; ///< Flag for dynamically swithing between debug and release mode
    SF_DEBUG_LEVEL debugLevel; ///< Debug level
    Uint64 startTime; ///< Time in nanoseconds from system start
} SfDebuggerContext;

/**
****************************************************************************************************
* @brief Initialize debugger service
* @param [in,out] pDebuggerContext Pointer to the debugger context
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SFLIB SF_STATUS SFAPI SfOpenDebuggerContext(SfDebuggerContext* pDebuggerContext);

/**
****************************************************************************************************
* @brief Close debugger service
* @param [in,out] pDebuggerContext Pointer to the debugger context
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SFLIB SF_STATUS SFAPI SfCloseDebuggerContext(SfDebuggerContext* pDebuggerContext);

/**
****************************************************************************************************
* @brief Handler for debug log messages
* @param [in] debugClass Class for debugging
* @param [in] debugLevel Message type /Error/Warning/Information
* @param [in] format Format string
* @param [in] arglist Arguments list
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SFLIB SF_STATUS SFAPI SfLogHandler(SF_DEBUG_CLASS debugClass, SF_DEBUG_LEVEL debugLevel,
                             const Char* format,	...);

/**
****************************************************************************************************
* @brief Output debug string to debug interface
* @param [in] pDebuggerContext Pointer to the debugger context
* @param [in] pMessage Debug message
*
* @return void
****************************************************************************************************
*/
SFLIB void SFAPI SfOutputDebugString(const SfDebuggerContext* pDebuggerContext, const Char* pMessage);

/**
****************************************************************************************************
* @brief Omit the debuger interrupt
*
* @return void
****************************************************************************************************
*/
SFLIB void SFAPI SfDebugBreak(void);

// #ifndef SF_BUILD_DEBUG
    
// #define SF_LOG_I(format, args...) LOGI("\033[33m [%s:%d]"fmt"\033[0m\n", __func__, __LINE__, ##args)
// #define SF_LOG_W(format, args...) LOGW("\033[32m [%s:%d]"fmt"\033[0m\n", __func__, __LINE__, ##args)
// #define SF_LOG_E(format, args...) LOGE("\033[31m [%s:%d]"fmt"\033[0m\n", __func__, __LINE__, ##args)

// #else

// #endif

#ifdef SF_BUILD_DEBUG

/**
****************************************************************************************************
* @brief Print message to the output stream
* @see SfLogHandler
****************************************************************************************************
*/
#define SF_LOG(debugClass, debugLevel, ...) \
    SfLogHandler(debugClass, debugLevel, __VA_ARGS__);

#ifndef SF_MOD_CLASS
    #define SF_LOG_I(...) \
        SF_LOG(SF_DEBUG_CLASS_UNDEFINED, SF_DEBUG_LEVEL_INFO, __VA_ARGS__);
    #define SF_LOG_E(...) \
        SF_LOG(SF_DEBUG_CLASS_UNDEFINED, SF_DEBUG_LEVEL_ERROR, __VA_ARGS__);
    #define SF_LOG_W(...) \
        SF_LOG(SF_DEBUG_CLASS_UNDEFINED, SF_DEBUG_LEVEL_WARNING, __VA_ARGS__);
#else
    #define SF_LOG_I(...) \
        SF_LOG(SF_MOD_CLASS, SF_DEBUG_LEVEL_INFO, __VA_ARGS__);
    #define SF_LOG_E(...) \
        SF_LOG(SF_MOD_CLASS, SF_DEBUG_LEVEL_ERROR, __VA_ARGS__);
    #define SF_LOG_W(...) \
        SF_LOG(SF_MOD_CLASS, SF_DEBUG_LEVEL_WARNING, __VA_ARGS__);
#endif  /* !SF_MOD_CLASS */

#define SF_ASSERT(debugClass, condition, ...) \
    if(!(condition)) \
    { \
    if(SF_FAILED(SfLogHandler(debugClass, SF_DEBUG_LEVEL_ERROR, __VA_ARGS__))) \
        { \
            /*SfDebugBreak();*/ ;\
        } \
    }
#else
    #define SF_ASSERT(debugClass, condition, ...)
    #define SF_LOG(debugClass, debugLevel, ...)
    #define SF_LOG_I(...)
    #define SF_LOG_E(...)
    #define SF_LOG_W(...)
#endif /* ! SF_BUILD_DEBUG  */

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif /* !_SF_DEBUG_H_ */
