/**
****************************************************************************************************
* @file SfNetlinkSerialization.h
* @brief Security framework [SF] netlink serialization
* @author Dmitriy Dorogovtsev(d.dorogovtse@samsung.com)
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Anton Skakun (a.skakun@samsung.com)
* @date Created May 23, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_NETLINK_SERIALIZATION_H_
#define _SF_NETLINK_SERIALIZATION_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#include <uapi/linux/sf/core/SfCore.h>

/**
****************************************************************************************************
* @struct SfNetlinkPacket
* @brief Encapsulate different level implementation of the netlink packet serailization
* @note This is internal structure
****************************************************************************************************
*/
typedef struct
{
#ifdef SF_LEVEL_KERNEL
	struct sk_buff* pBuffer; ///< Kernel space level socket buffer
#else
	struct nl_msg* pBuffer; ///< User space level socket buffer
#endif	/* !SF_LEVEL_KERNEL */
} SfNetlinkPacket;

/**
****************************************************************************************************
* @brief Create new internal netlink packet
* @warning This function allocate memory. Refer to SfDestroyNetlinkPacket to free resources.
* @return New allocated netlink packet on success, NULL otherwise
****************************************************************************************************
*/
SfNetlinkPacket* SfCreateNetlinkPacket(void);

/**
****************************************************************************************************
* @brief Destroy internal netlink packet
* @param [in] pPacket Pointer to the  internal netlink packet
* @return void
****************************************************************************************************
*/
void SfDestroyNetlinkPacket(SfNetlinkPacket* const pPacket);

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif	/* !_SF_NETLINK_SERIALIZATION_H_ */
