/**
****************************************************************************************************
* @file SfTime.h
* @brief Security framework [SF] time functions definitions
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com) 
* @date Created Mar 6, 2014 9:00
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_TIME_H_
#define _SF_TIME_H_

#include "SfConfig.h"
#include "SfTypes.h"

/**
****************************************************************************************************
* @brief This struct represent time
****************************************************************************************************
*/
typedef struct
{
    Uint32	days; ///< Days
    Uint8	hours; ///< Hours
    Uint8	minutes; ///< Minutes
    Uint8	sec; ///< Seconds
    Uint32	msec; ///< Miliseconds
    Uint32	usec; ///< Microseconds
} SfTime;

/**
****************************************************************************************************
* @brief Define one second in miliseconds
****************************************************************************************************
*/
static const Ulong c_second = 1000L;

/**
****************************************************************************************************
* @brief Get time in nanosecod from system start
*
* @return System timestamp in nanosecond
****************************************************************************************************
*/
Uint64 SFAPI SfGetSystemTimeUsec(void);

/**
****************************************************************************************************
* @brief Convert nanosecond timestamp into time structure
* @return void
****************************************************************************************************
*/
void SFAPI SfParseTimeStructure(SfTime* time, Uint64 rsec);

/**
****************************************************************************************************
* @brief Sleep milisecond times
* @return void
****************************************************************************************************
*/
void SFAPI SfSleepMs(Ulong timeMs);

#endif	/* !_SF_TIME_H_ */
