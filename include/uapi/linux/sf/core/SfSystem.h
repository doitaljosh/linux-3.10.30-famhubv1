/**
****************************************************************************************************
* @file SfSystem.h
* @brief Security framework [SF] OS related config
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 17:07
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_SYSTEM_H_
#define _SF_SYSTEM_H_

#if defined(_MSC_VER)

/**
****************************************************************************************************
* @brief Declares Windows OS family
****************************************************************************************************
*/
#define SF_OS_WINDOWS
#include <windows.h>
#define sf_snprintf sprintf_s
#elif defined(__linux__)

/**
****************************************************************************************************
* @brief Declares Linux OS family
****************************************************************************************************
*/
#define SF_OS_LINUX
#include <linux/string.h>
#define sf_snprintf snprintf
#else
#error Unknown Operating System. Refer to 'SfSystem.h' file.
#endif

#endif	/* !_SF_SYSTEM_H_ */
