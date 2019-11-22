
#include "SfdTimer.h"
#include <uapi/linux/sf/core/SfDebug.h>

#if defined(SECURITY_SFD_LEVEL_DEBUG)

/*
****************************************************************************************************
*
****************************************************************************************************
*/
static void SfdPrintTimeSpecStructure(const struct timespec* const t, const char* const log_string)
{
	unsigned int ms, us, ns;
	unsigned long int nsec = t->tv_nsec;
	ns = nsec % 1000;
	nsec /= 1000;
	us = nsec % 1000;
	nsec /= 1000;
	ms = nsec % 1000;

	SF_LOG_I( "\t%s %lus %ums %uus %uns\n", log_string, t->tv_sec, ms, us, ns );
}

#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)

/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdTimerSetName(SfdTimer* const pTimer, const char* const name)
{
	snprintf(pTimer->name, SFD_TIMER_NAME_LENGTH, "%s", name);
}

#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdTimerReset(SfdTimer* const pTimer)
{
	pTimer->currentTestNumber = 0;
	pTimer->sum.tv_sec = 0;
	pTimer->sum.tv_nsec = 0;
}
#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdTimerStart(SfdTimer* const pTimer)
{
	getnstimeofday(&pTimer->time_start);
}
#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdTimerStop(SfdTimer* const pTimer)
{
	struct timespec ts_delta;
	getnstimeofday( &pTimer->time_end );

	ts_delta = timespec_sub( pTimer->time_end, pTimer->time_start );
	pTimer->sum = timespec_add( pTimer->sum, ts_delta );
	pTimer->currentTestNumber++;

	if ( 1 == pTimer->currentTestNumber )
	{
		pTimer->time_min = ts_delta;
		pTimer->time_max = ts_delta;
	}
	else
	{
		if ( timespec_compare( &ts_delta, &pTimer->time_min ) < 0 )
			pTimer->time_min = ts_delta;
		if ( timespec_compare( &ts_delta, &pTimer->time_max ) > 0 )
			pTimer->time_max = ts_delta;
	}
}
#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/*
****************************************************************************************************
*
****************************************************************************************************
*/
void SfdTimerShow(SfdTimer* const pTimer)
{
	if ( 0 == pTimer->currentTestNumber )
	{
		SF_LOG_I( "Timer %s has no measurements\n", pTimer->name );
	}
	else
	{
		struct timespec avg;
		u64 ns = (u64)( pTimer->sum.tv_sec ) * NSEC_PER_SEC + pTimer->sum.tv_nsec;
		avg = ns_to_timespec( div_u64( ns, pTimer->currentTestNumber ) );

		SF_LOG_I( "Timer %s: %u measurements\n", pTimer->name, pTimer->currentTestNumber );
		SfdPrintTimeSpecStructure( &pTimer->time_min, "min:" );
		SfdPrintTimeSpecStructure( &avg, "avg:" );
		SfdPrintTimeSpecStructure( &pTimer->time_max, "max:" );
	}
}
#endif
