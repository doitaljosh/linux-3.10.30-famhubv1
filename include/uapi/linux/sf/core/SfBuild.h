/**
****************************************************************************************************
* @file SfBuild.h
* @brief Security framework [SF] build related config
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Apr 1, 2014 17:07
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_BUILD_H_
#define _SF_BUILD_H_

#if defined(DEBUG_BUILD)

#define SF_BUILD_DEBUG
#elif defined(RELEASE_BUILD)

#define SF_BUILD_RELEASE
#else
#error Unknown build. Please, refer to SfMode.h file.
#endif

#endif	/* !_SF_BUILD_H_ */
