/**
****************************************************************************************************
* @file SfLevel.h
* @brief Security framework [SF] execution environment related config
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 17:07
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_LEVEL_H_
#define _SF_LEVEL_H_

#if defined(KS_MODULE)

/**
****************************************************************************************************
* @brief Declares kernel level execution environment
****************************************************************************************************
*/
#define SF_LEVEL_KERNEL
#elif defined(US_MODULE)

/**
****************************************************************************************************
* @brief Declares user level execution environment
****************************************************************************************************
*/
#define SF_LEVEL_USER
#elif defined(TZ_MODULE)

/**
****************************************************************************************************
* @brief Declares trust-zone execution environment
****************************************************************************************************
*/
#define SF_LEVEL_TZ
#error This mode currently unsupported. Are you sure ? If yes, press any button %)
#else
#error Unknown execution environment. Refer to 'SfLevel.h' file.
#endif /* KERNEL || USER || TZ */

#endif	/* !_SF_LEVEL_H_ */
