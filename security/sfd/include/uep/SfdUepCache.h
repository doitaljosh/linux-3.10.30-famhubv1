/**
****************************************************************************************************
* @file SfdUepCache.h
* @brief Security framework [SF] filter driver [D] chaching routine declaration
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Oct 10, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_UEP_CACHE_H_
#define _SFD_UEP_CACHE_H_

#include <uapi/linux/sf/core/SfCore.h>

struct file;

/**
****************************************************************************************************
* @brief Check cached data for the file that is going to be verified.
* @param [in] pFile Pointer to the file to checked
* @note This function depends on CONFIG_SECURITY_SFD_UEP_SIGNATURE_CACHE. It must be specified by
*	the Kconfig. In case if caching disabled this function returns SF_STATUS_FAIL.
* @warning This function doesn't check pFile on NULL pointer since, it MUST be checked in function
*	where SfdUepCheckCachedData called. Only f_inode member will be checked for NULL pointer.
* @return SF_STATUS_OK if file was found in cached data, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepCheckCachedData(const struct file* const pFile);

/**
****************************************************************************************************
* @brief Add cache data to file to speed up signature verification.
* @param [in] pFile Pointer to the file to be updated
* @warning Result of this function will be ignored in DEBUG and RELEASE mode. Many issues may
*	happen, but signature verification must work. If this function return fail, it means that
*	that something wrong with kernel and caching will be unavailable.
* @return SF_STATUS_OK on success, SF_STATUS_FAIL - otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepAddCacheData(struct file* const pFile);

#endif /* !_SFD_UEP_CACHE_H_ */
