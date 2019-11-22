/**
****************************************************************************************************
* @file SfdUepCache.c
* @brief Security framework [SF] filter driver [D] Caching routine implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Oct 10, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include "SfdUepCache.h"
#include <linux/fs.h>

#if defined(CONFIG_SECURITY_SFD_UEP_SIGNATURE_CACHE)

#define SFD_UEP_CACHE_MAGIC		0xc001c001


/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepCheckCachedData(const struct file* const pFile)
{
	SF_STATUS result = SF_STATUS_FAIL;
	SF_LOG_I("pFile Pointer: %p", pFile);
	SF_LOG_I("%s: %d cached data", __FUNCTION__, pFile->f_inode->i_version);
	do
	{
		if (NULL == pFile->f_inode)
		{
			break;
		}

		/**
		* @note Before file was accesses first time in the system session,
		*	file version may be zero.
		*/
		if (0 == pFile->f_inode->i_version)
		{
			break;
		}

		if ((pFile->f_inode->i_version - pFile->f_inode->i_ino) == 1)
		{
			result = SF_STATUS_OK;
			break;	
		}

	} while(FALSE);

	return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepAddCacheData(struct file* const pFile)
{
	SF_STATUS result = SF_STATUS_FAIL;

	if (pFile->f_inode != NULL)
	{
		pFile->f_inode->i_version++;
		result = SF_STATUS_OK;
	}

	return result;
}

#else

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepCheckCachedData(const struct file* const pFile)
{
	/**
	* @note This function return fail everytime, since caching algorithm disabled by Kconfig.
	*/
	return SF_STATUS_FAIL;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUepAddCacheData(struct file* const pFile)
{
	/**
	* @note This function return fail everytime, since caching algorithm disabled by Kconfig.
	*/
	return SF_STATUS_FAIL;
}


#endif /* !CONFIG_SECURITY_SFD_UEP_SIGNATURE_CACHE */