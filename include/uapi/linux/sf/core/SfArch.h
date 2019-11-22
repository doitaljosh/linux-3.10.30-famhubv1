/**
****************************************************************************************************
* @file SfArch.h
* @brief Security framework [SF] CPU architecture related config
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Mar 14, 2014 17:07
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_ARCH_H_
#define _SF_ARCH_H_

#if defined(__arm__) || defined(__TARGET_ARCH_ARM) || defined(_M_ARM)

/**
****************************************************************************************************
* @brief Declares ARM architecuture evironment
****************************************************************************************************
*/
#define SF_ARCH_ARM
#define SF_LEFT_SCANING_BORDER			0xc0000000
#define SF_RIGHT_SCANING_BORDER			0xd0000000
#define SF_VIRTUAL_ADDRESS_OFFSET_MASK 	0xfff
#elif defined(__amd64__) || defined(__x86_64__) || defined(_M_X64)

/**
****************************************************************************************************
* @brief Declares X86_64 and/or AMD64 architecture environment
****************************************************************************************************
*/
#define SF_ARCH_X64
#define SF_LEFT_SCANING_BORDER			0xffffffff81000000
#define SF_RIGHT_SCANING_BORDER			0xffffffff82000000
#define SF_VIRTUAL_ADDRESS_OFFSET_MASK 	0xfff
#elif defined(__i386__) || defined(_X86_) || defined(_M_I86) || defined(_WIN32)

/**
****************************************************************************************************
* @brief Declares X86 architecture environment
****************************************************************************************************
*/
#define SF_ARCH_X86
#define SF_LEFT_SCANING_BORDER			0xc0000000
#define SF_RIGHT_SCANING_BORDER			0xd0000000
#define SF_VIRTUAL_ADDRESS_OFFSET_MASK 	0xfff
#else
#error Unknown architecture. Refer to 'SfArch.h' file.
#endif /* !_X64_ || !_X86_ || !_ARM_ */

#endif	/* !_SF_ARCH_H_ */
