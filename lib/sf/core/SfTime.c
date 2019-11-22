/**
****************************************************************************************************
* @file SfTime.c
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

// local
#include <uapi/linux/sf/core/SfTime.h>
#include <linux/time.h>
#include <linux/delay.h>

/**
****************************************************************************************************
*
****************************************************************************************************
*/
Uint64 SFAPI SfGetSystemTimeUsec(void)
{
    Uint64 usecTime = 0;
    Uint32 rem = 0;
    struct timespec timeOfDay;

    getnstimeofday( &timeOfDay );
    usecTime  = (Uint64)timeOfDay.tv_sec * 1000000L;
    usecTime += div_u64_rem( (Uint64)timeOfDay.tv_nsec, 1000, &rem );
    return usecTime;
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SFAPI SfSleepMs(Ulong timeMs)
{
    msleep(timeMs);
}

/**
****************************************************************************************************
*
****************************************************************************************************
*/
void SFAPI SfParseTimeStructure(SfTime* pTime, Uint64 rsec)
{
    Uint32 sec = 0, min = 0, hours = 0;

    rsec = div_u64_rem( rsec, 1000, &pTime->usec );
    rsec = div_u64_rem( rsec, 1000, &pTime->msec );
    rsec = div_u64_rem( rsec, 60, &sec );
    rsec = div_u64_rem( rsec, 60, &min );
    rsec = div_u64_rem( rsec, 24, &hours );

    pTime->sec     = sec;
    pTime->minutes = min;
    pTime->hours   = hours;
    pTime->days    = (Uint32)rsec;
}
