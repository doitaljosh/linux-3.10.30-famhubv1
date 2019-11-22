/**
****************************************************************************************************
* @file SfVersion.h
* @brief Security framework [SF] common configurations file
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Mar 13, 2014 19:27
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_VERSION_H_
#define _SF_VERSION_H_

/**
****************************************************************************************************
* @brief Minor version of the SW (SRK internal version)
****************************************************************************************************
*/
#define SF_MINOR_VERSION 0007

/**
****************************************************************************************************
* @brief Major version of the SW (HQ public version)
****************************************************************************************************
*/
#define SF_MAJOR_VERSION 0001

/**
****************************************************************************************************
* @brief Makes build version
****************************************************************************************************
*/
#define GENERATE_VERSION(MAJOR, MINOR) (((MAJOR & 0x0000ffff) << 15) | ((MINOR) & 0x0000ffff))

/**
****************************************************************************************************
* @brief Build version
****************************************************************************************************
*/
#define SF_CORE_VERSION GENERATE_VERSION(SF_MAJOR_VERSION, SF_MINOR_VERSION)

#endif	/* !_SF_VERSION_H_ */
