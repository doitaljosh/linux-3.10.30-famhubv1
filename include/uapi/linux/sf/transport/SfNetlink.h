/**
****************************************************************************************************
* @file SfdNetlinkSetup.h
* @brief Security framework [SF] filter driver [D] Netlink constants
* @author Dmitriy Dorogovtsev(d.dorogovtse@samsung.com)
* @date Created Apr 15, 2014
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/
#ifndef _SFD_NETLINK_SETUP_H_
#define _SFD_NETLINK_SETUP_H_

/**
****************************************************************************************************
* @enum                                 SfdPacketAttribute
* @brief                                Specifies Netlink attributes of SF packet structure
****************************************************************************************************
*/
typedef enum
{
    SFD_PACKET_UNSPEC_ATTR,             ///< Packet unspecified attribute
    SFD_PACKET_HEADER_ATTR,             ///< Packet header attribute
    SFD_PACKET_ENVIRONMENT_ATTR,        ///< Packet environment attribute
    SFD_PACKET_OPERATION_ATTR,          ///< Packet operation attribute
    SFD_PACKET_MAX_ATTR__               ///< Packet attribute count
} SfdPacketAttribute;
#define SFD_PACKET_MAX_ATTR (SFD_PACKET_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdProtocolHeaderAttribute
* @brief                                Specifies Netlink attributes of SF protocol header structure
****************************************************************************************************
*/
typedef enum
{
    SFD_PROTOCOL_HEADER_UNSPEC_ATTR,    ///< ProtocolHeader unspecified attribute
    SFD_PROTOCOL_HEADER_SIZE_ATTR,      ///< ProtocolHeader size attribute
    SFD_PROTOCOL_HEADER_TYPE_ATTR,      ///< ProtocolHeader type attribute
    SFD_PROTOCOL_HEADER_MAX_ATTR__      ///< ProtocolHeader attribute count
} SfdProtocolHeaderAttribute;
#define SFD_PROTOCOL_HEADER_MAX_ATTR (SFD_PROTOCOL_HEADER_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdEnvironmentAttribute
* @brief                                Specifies Netlink attributes of general environment
****************************************************************************************************
*/
typedef enum
{
    SFD_ENV_UNSPEC_ATTR,                ///< Packet environment unspecified attribute
    SFD_ENV_HEADER_ATTR,                ///< Packet environment header attribute
    SFD_ENV_DATA_ATTR,                  ///< Packet environment data attribute
    SFD_ENV_MAX_ATTR__                  ///< Packet environment attribute count
} SfdEnvironmentAttribute;
#define SFD_ENV_MAX_ATTR (SFD_ENV_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdOperationAttribute
* @brief                                Specifies Netlink attributes of packet operation
****************************************************************************************************
*/
typedef enum
{
    SFD_OP_UNSPEC_ATTR,                 ///< Packet operation unspecified attribute
    SFD_OP_HEADER_ATTR,                 ///< Packet operation header attribute
    SFD_OP_DATA_ATTR,                   ///< Packet operation data attribute
    SFD_OP_MAX_ATTR__                   ///< Packet operation attribute count
} SfdOperationAttribute;
#define SFD_OP_MAX_ATTR (SFD_OP_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdOperationBlockRuleAttribute
* @brief                                Specifies Netlink attributes of block rule operation
****************************************************************************************************
*/
typedef enum
{
    SFD_OP_RULE_UNSPEC_ATTR,            ///< Block rule operation unspecified attribute
    SFD_OP_RULE_TYPE_ATTR,              ///< Block rule operation rule type attribute
    SFD_OP_RULE_ACTION_ATTR,            ///< Block rule operation action attribute
    SFD_OP_RULE_ADDR_ATTR,              ///< Block rule operation IPv4 address attribute
    SFD_OP_RULE_INODE_ATTR,             ///< Block rule operation file inode attribute
    SFD_OP_RULE_MAX_ATTR__              ///< Block rule operation attribute count
} SfdOperationBlockRuleAttribute;
#define SFD_OP_RULE_MAX_ATTR (SFD_OP_RULE_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdOperationSetupDUIDAttribute
* @brief                                Specifies Netlink attributes of setup DUID operation
****************************************************************************************************
*/
typedef enum
{
    SFD_OP_DUID_UNSPEC_ATTR,            ///< Setup DUID operation unspecified attribute
    SFD_OP_DUID_DUID_ATTR,              ///< Setup DUID operation DUID content attribute
    SFD_OP_DUID_MAX_ATTR__              ///< Setup DUID operation attribute count
} SfdOperationSetupDUIDAttribute;
#define SFD_OP_DUID_MAX_ATTR (SFD_OP_DUID_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdEnvironmentHeaderAttribute
* @brief                                Specifies Netlink attributes of environment header
****************************************************************************************************
*/
typedef enum
{
    SFD_ENV_HEADER_UNSPEC_ATTR,         ///< Environment header unspecified attribute
    SFD_ENV_HEADER_SIZE_ATTR,           ///< Environment header size attribute
    SFD_ENV_HEADER_TYPE_ATTR,           ///< Environment header type attribute
    SFD_ENV_HEADER_MAX_ATTR__           ///< Environment header attribute count
} SfdEnvironmentHeaderAttribute;
#define SFD_ENV_HEADER_MAX_ATTR (SFD_ENV_HEADER_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdExecutionEnvironmentAttribute
* @brief                                Specifies Netlink attributes of execution environment
****************************************************************************************************
*/
typedef enum
{
    SFD_EXEC_ENV_UNSPEC_ATTR,           ///< Execution environment unspecified attribute
    SFD_EXEC_ENV_PROCESS_NAME_ATTR,     ///< Calling process name attribute
    SFD_EXEC_ENV_PROCESS_ID_ATTR,       ///< Calling process ID attribute
    SFD_EXEC_ENV_TIME_ATTR,             ///< Time of syscall attribute
    SFD_EXEC_ENV_RESULT_ATTR,           ///< Syscall result attribute
    SFD_EXEC_ENV_MAX_ATTR__             ///< Execution environment attribute count
} SfdExecutionEnvironmentAttribute;
#define SFD_EXEC_ENV_MAX_ATTR (SFD_EXEC_ENV_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdFileEnvironmentAttribute
* @brief                                Specifies Netlink attributes of file environment
****************************************************************************************************
*/
typedef enum
{
    SFD_FILE_ENV_UNSPEC_ATTR,           ///< File environment unspecified attribute
    SFD_FILE_ENV_EXEC_ENV_ATTR,         ///< Execution environment attribute
    SFD_FILE_ENV_FILE_NAME_ATTR,        ///< Accessed file name attribute
    SFD_FILE_ENV_INODE_ATTR,            ///< Accessed file inode attribute
    SFD_FILE_ENV_MAX_ATTR__             ///< File environment attribute count
} SfdFileEnvironmentAttribute;
#define SFD_FILE_ENV_MAX_ATTR (SFD_FILE_ENV_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdProcessEnvironmentAttribute
* @brief                                Specifies Netlink attributes of process environment
****************************************************************************************************
*/
typedef enum
{
    SFD_PROC_ENV_UNSPEC_ATTR,           ///< Process environment unspecified attribute
    SFD_PROC_ENV_EXEC_ENV_ATTR,         ///< Execution environment attribute
    SFD_PROC_ENV_PROCESS_NAME_ATTR,     ///< Launched process name attribute
    SFD_PROC_ENV_PROCESS_INODE_ATTR,    ///< Launched process inode attribute
    SFD_PROC_ENV_MAX_ATTR__             ///< Process environment attribute count
} SfdProcessEnvironmentAttribute;
#define SFD_PROC_ENV_MAX_ATTR (SFD_PROC_ENV_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdNetworkEnvironmentAttribute
* @brief                                Specifies Netlink attributes of network environment
****************************************************************************************************
*/
typedef enum
{
    SFD_NET_ENV_UNSPEC_ATTR,            ///< Network environment unspecified attribute
    SFD_NET_ENV_EXEC_ENV_ATTR,          ///< Execution environment attribute
    SFD_NET_ENV_ADDR_ATTR,              ///< Address attribute
    SFD_NET_ENV_PORT_ATTR,              ///< Port attribute
    SFD_NET_ENV_MAX_ATTR__              ///< Network environment attribute count
} SfdNetworkEnvironmentAttribute;
#define SFD_NET_ENV_MAX_ATTR (SFD_NET_ENV_MAX_ATTR__ - 1)

/**
****************************************************************************************************
* @enum                                 SfdMmapEnvironmentAttribute
* @brief                                Specifies Netlink attributes of mmap() environment
****************************************************************************************************
*/
typedef enum
{
    SFD_MMAP_ENV_UNSPEC_ATTR,           ///< Mmap environment unspecified attribute
    SFD_MMAP_ENV_EXEC_ENV_ATTR,         ///< Execution environment attribute
    SFD_MMAP_ENV_LIBRARY_NAME_ATTR,     ///< Mapped library name attribute
    SFD_MMAP_ENV_INODE_ATTR,            ///< Mapped library inode attribute
    SFD_MMAP_ENV_MAX_ATTR__             ///< Mmap environment attribute count
} SfdMmapEnvironmentAttribute;
#define SFD_MMAP_ENV_MAX_ATTR (SFD_MMAP_ENV_MAX_ATTR__ - 1)

#endif  /* _SFD_NETLINK_SETUP_H_ */
