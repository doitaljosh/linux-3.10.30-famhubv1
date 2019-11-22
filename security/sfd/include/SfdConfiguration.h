/**
****************************************************************************************************
* @file SfDebug.h
* @brief Security framework [SF] debug implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Sep 25, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SFD_CONFIGURATION_H_
#define _SFD_CONFIGURATION_H_

#include <uapi/linux/sf/core/SfCore.h>
#include <uapi/asm-generic/errno-base.h>

#if defined(CONFIG_SECURITY_SFD_MODE_PERMISSIVE)

/**
****************************************************************************************************
* @brief Convert SF result to the system native value
* @param [in] result Value to be returned to the system
* @note This function depends on configuration. Check Security Filter Driver Working modes
* @return System error code
****************************************************************************************************
*/
static inline int SfReturn(const SF_STATUS result)
{
	/**
	* @todo Add log message here to show the real result and inform about permissive mode
	*/
	return 0;
}

#endif

#if defined(CONFIG_SECURITY_SFD_MODE_ENFORCE)

/**
****************************************************************************************************
* @brief Convert SF result to the system native value
* @param [in] result Value to be returned to the system
* @note This function depends on configuration. Check Security Filter Driver Working modes
* @return System error code
****************************************************************************************************
*/
static inline int SfReturn(const SF_STATUS result)
{
	int systemResult = 0;	// default path

	switch(result)
	{
		case SF_STATUS_UEP_SIGNATURE_INCORRECT:
		case SF_STATUS_UEP_FILE_NOT_SIGNED:
		case SF_STATUS_RESOURCE_BLOCK:
		{
			systemResult = -EPERM;
		}
		break;

		default:
			systemResult = 0;
		break;
	}

	return systemResult;
}

#endif

struct file;
struct path;
struct task_struct;

/**
****************************************************************************************************
* @brief Check configuration of the system to decide should we process this file.
* @param [in] pFile Pointer to the file to be processed.
* @return SF_STATUS_OK if the file is in responsibility of SFD, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SfCheckModuleResponsibility(struct file* const pFile);

/**
****************************************************************************************************
* @brief                        Construct absolute file name by path object
* @param [in] pPath              Path associated with file
* @param [out] ppName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByPath(struct path* const pPath, Char** const ppName);

/**
****************************************************************************************************
* @brief                        Construct absolute file name by file object
* @param [in] pFile             Pointer to the file structure
* @param [out] ppName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SfConstructAbsoluteFileNameByFile(struct file* const pFile, Char** const ppName);

/**
****************************************************************************************************
* @brief                        Construct absolute file name by process context
* @param [in] pProcessContext   Pointer to the process context object
* @param [out] ppName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByTask(const struct task_struct* const pProcessContext,
	Char** const ppName);



#endif	/* !_SFD_CONFIGURATION_H_ */
