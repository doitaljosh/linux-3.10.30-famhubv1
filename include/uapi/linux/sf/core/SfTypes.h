/**
****************************************************************************************************
* @file SfTypes.h
* @brief Security framework [SF] core types definitions
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Mar 4, 2014 9:00
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/
#ifndef _SF_TYPES_H_
#define _SF_TYPES_H_

#include "SfConfig.h"

/*
****************************************************************************************************
* @brief Security framework [SF] core types definitions
* @see Coding standard 2.3 "Variable/Function parameters naming conversion"
****************************************************************************************************
*/
#if defined(SF_OS_WINDOWS) 

    typedef signed   int	Int;
    typedef unsigned int	Uint;
    typedef signed long     Long;
    typedef unsigned long   Ulong;
    typedef char			Char;
    typedef unsigned char	Uchar;

    typedef int				Bool;
    #define TRUE            1
    #define FALSE			0

    typedef signed   _int8	Int8;
    typedef unsigned _int8	Uint8;

    typedef signed   _int16 Int16;
    typedef unsigned _int16 Uint16;

    typedef signed   _int32 Int32;
    typedef unsigned _int32 Uint32;

    typedef signed   _int64 Int64;
    typedef unsigned _int64 Uint64;

#elif defined (SF_OS_LINUX)
    #include <linux/types.h>
    typedef signed int		Int;
    typedef unsigned int	Uint;
    typedef signed long     Long;
    typedef unsigned long   Ulong;
    typedef char			Char;
    typedef unsigned char	Uchar;

    typedef int				Bool;
    #define TRUE			1
    #define FALSE			0

    typedef int8_t			Int8;
    typedef uint8_t			Uint8;

    typedef int16_t			Int16;
    typedef uint16_t		Uint16;

    typedef int32_t			Int32;
    typedef uint32_t		Uint32;

    typedef int64_t			Int64;
    typedef uint64_t		Uint64;

#else
    #error Unknown OS not supported
#endif /* !SF_OS_WINDOWS && !SF_OS_LINUX */

/**
****************************************************************************************************
* @brief NULL value definition
****************************************************************************************************
*/
#ifndef NULL
    #define NULL 0
#endif /* !NULL */

/**
****************************************************************************************************
* @brief Security framework context state implementation
****************************************************************************************************
*/
typedef enum 
{
    SF_CONTEXT_STATE_UNINITIALIZED  = 0,
    SF_CONTEXT_STATE_INITIALIZED    = 1,
    SF_CONTEXT_STATE_MAX            = SF_CONTEXT_STATE_INITIALIZED
} SF_CONTEXT_STATE;

/**
****************************************************************************************************
* @brief This struct used as header for each contexts structures
****************************************************************************************************
*/
typedef struct
{
    Uint32  size; ///< Size of the context in bytes
    Uint32  version; ///< Context version
    Uint32  state; ///< Context state (0 means uninitialized and non-zero initialized)
} SfContextHeader;

#endif /* !_SF_TYPES_H_ */
