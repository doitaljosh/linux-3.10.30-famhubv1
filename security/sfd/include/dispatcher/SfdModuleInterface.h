/**
****************************************************************************************************
* @file SfdModuleInterface.h
* @brief Security framework [SF] filter driver [D] main structure that contains all submodules
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 9, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef	_SFD_MODULE_INTERFACE_H_
#define _SFD_MODULE_INTERFACE_H_

#include <uapi/linux/sf/transport/SfTransport.h>
#include <linux/list.h>

/**
****************************************************************************************************
* @enum SFD_MODULE_TYPE
* @brief Module type enumeration
****************************************************************************************************
*/
typedef enum
{
	SFD_MODULE_TYPE_DISPATCHER = 0, ///< Dispatcher module type
	SFD_MODULE_TYPE_NOTIFIER = 1, ///< Notifier module type
	SFD_MODULE_TYPE_CONTAINER = 2, ///< Container module type
	SFD_MODULE_TYPE_UEP = 3, ///< UEP module type
	SFD_MODULE_TYPE_MAX = SFD_MODULE_TYPE_UEP

} SFD_MODULE_TYPE;

/**
****************************************************************************************************
* @enum SFD_PACKET_HANDLER_TYPE
* @brief Packet handler type. Packet handler may be preventive when result of the handler will be
*	returned to the system or notification handler when result of the handler will be ignored.
****************************************************************************************************
*/
typedef enum
{
	SFD_PACKET_HANDLER_TYPE_PREVENTIVE = 0,
	SFD_PACKET_HANDLER_TYPE_NOTIFICATION = 1,
	SFD_PACKET_HANDLER_TYPE_MAX
} SFD_PACKET_HANDLER_TYPE;

/**
****************************************************************************************************
* @brief Module interface is used to register components such UEP, Notifier, Container and other
* 	in Dispathcer.
****************************************************************************************************
*/
typedef struct __packed
{
	/**
	* @brief Module type.
	* @warning SFD_MODULE_TYPE enumeration MUST be used to operate with this structure.
	*/
	Uint32 				moduleType;
	SfPacketHandler 	PacketHandler[SFD_PACKET_HANDLER_TYPE_MAX]; ///< Module packet handlers
	
	SfNode* 			pNode; ///< Pointer to the SFD transport struct
	struct list_head	list; ///< Module list entry

} SfdModuleInterface;

/**
****************************************************************************************************
* @brief Register sub-module in the dispatcher
* @param [in] pInterface Pointer to the module interface
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdRegisterModule(SfdModuleInterface* const pInterface);

/**
****************************************************************************************************
* @brief Unregister sub-module in the dispathcer
* @param [in] pInterface Pointer to the debugger context
*
* @return SF_STATUS_OK on success,  SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdUnregisterModule(SfdModuleInterface* const pInterface);

#endif	/*	!_SFD_MODULE_INTERFACE_H_ */
