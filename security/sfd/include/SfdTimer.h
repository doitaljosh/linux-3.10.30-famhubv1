
#ifndef _SFD_TIMER_H_
#define _SFD_TIMER_H_

#include <linux/time.h>
#include <uapi/linux/sf/core/SfCore.h>

/**
****************************************************************************************************
* @def SFD_TIMER_NAME_MAX_LENGTH
* @brief Size of the name for the timer structure
****************************************************************************************************
*/
#define SFD_TIMER_NAME_LENGTH	32

/**
****************************************************************************************************
* @struct SfdTimer
* @brief Timer with 
****************************************************************************************************
*/
typedef struct
{
	struct timespec time_start; ///< Start time
	struct timespec time_end; ///< End time
	struct timespec sum; ///< Average time
	struct timespec time_min; ///< Minimal time
	struct timespec time_max; ///< Maximum time

	Uint32 currentTestNumber; ///< Current test number
	Uint32 numberOfTests; ///< Number of tests to be made

	/**
	* @brief Name of the timer
	*/
	char name[SFD_TIMER_NAME_LENGTH];
} SfdTimer;

#if defined(SECURITY_SFD_LEVEL_DEBUG)

/**
****************************************************************************************************
* @brief Set name to the timer structure
* @param [in] pTimer Pointer to the timer structure
* @param [in] name Name to be assigned for the 'pTimer'
* @return void
****************************************************************************************************
*/
void SfdTimerSetName(SfdTimer* const pTimer, const char* const name);

#else

/**
****************************************************************************************************
* @see SfdTimerSetName implementation for DEBUG mode.
* @note Release mode make profiler disabled
****************************************************************************************************
*/
static inline void SfdTimerSetName(SfdTimer* const pTimer, const char* const name)
{

}
#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)

/**
****************************************************************************************************
* @brief Reset timer structure
* @param [in] pTimer Pointer to the timer structuer to be reset
* @return void
****************************************************************************************************
*/
void SfdTimerReset(SfdTimer* const pTimer);
#else

/**
****************************************************************************************************
* @see SfdTimerReset implementation for DEBUG mode.
* @note Release mode make profiler disabled
****************************************************************************************************
*/
static inline void SfdTimerReset(SfdTimer* const pTimer)
{

}

#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/**
****************************************************************************************************
* @brief Start timer
* @param [in] pTimer Pointer to the timer
* @return void
****************************************************************************************************
*/
void SfdTimerStart(SfdTimer* const pTimer);
#else

/**
****************************************************************************************************
* @see SfdTimerStart implementation for DEBUG mode.
* @note Release mode make profiler disabled
****************************************************************************************************
*/
static inline void SfdTimerStart(SfdTimer* const pTimer)
{

}

#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)
/**
****************************************************************************************************
* @brief Stop timer
* @param [in] pTimer Pointer to the timer
* @return void
****************************************************************************************************
*/
void SfdTimerStop(SfdTimer* const pTimer);
#else

/**
****************************************************************************************************
* @see SfdTimerStop implementation for DEBUG mode.
* @note Release mode make profiler disabled
****************************************************************************************************
*/
static inline void SfdTimerStop(SfdTimer* const pTimer)
{

}

#endif

#if defined(SECURITY_SFD_LEVEL_DEBUG)

/**
****************************************************************************************************
* @brief Print results of the timer
* @param [in] pTimer Pointer to the timer
* @return void
****************************************************************************************************
*/
void SfdTimerShow(SfdTimer* const pTimer);
#else

/**
****************************************************************************************************
* @see SfdTimerShow implementation for DEBUG mode.
* @note Release mode make profiler disabled
****************************************************************************************************
*/
static inline void SfdTimerShow(SfdTimer* const pTimer)
{

}

#endif

#endif /* !_SFD_TIMER_H_ */
