/**
****************************************************************************************************
* @file SfEnvironmentFormat.h
* @brief Security framework [SF] filter driver [D] environment format structure implementation
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)
* @date Created Apr 18, 2014 16:43
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_ENVIRONMENT_FORMAT_H_
#define _SF_ENVIRONMENT_FORMAT_H_

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

#include "SfProtocolHeader.h"
#include "SfOperationsFormat.h"

/**
****************************************************************************************************
* @brief This macro create environment of of the given type
* @param [in] environment Environment data type
* @param [in] environmentType Enumeration value for coresponding data type
* @return Pointer to the environment header of the environment type on success, NULL otherwise
****************************************************************************************************
*/
#define SF_CREATE_ENVIRONMENT(environment, environmentType) \
    ({ \
    SfProtocolHeader* pEnvironment = sf_malloc( sizeof( environment ) ); \
    if ( NULL != pEnvironment ) \
    { \
        sf_zero_memory( pEnvironment, sizeof( environment ) ); \
        ((environment*)pEnvironment)->header.size = sizeof ( environment ); \
        ((environment*)pEnvironment)->header.type = environmentType; \
    } \
    else \
    { \
        SF_LOG_E( "%s can not allocate memory for the network environment structure", \
            __FUNCTION__ ); \
    } \
    pEnvironment; \
    })

/**
****************************************************************************************************
* @brief Init execution environment
****************************************************************************************************
*/
#define SF_INIT_SFEXECUTION_ENVIRONMENT \
({ \
    SfExecutionEnvironmentInfo eEnv; \
    eEnv.pProcessName = NULL; \
    eEnv.processId = 0; \
    eEnv.timeStamp = 0; \
    eEnv.sysCallResult = 0; \
    eEnv; \
})

/**
****************************************************************************************************
* @brief Init file environment
****************************************************************************************************
*/
#define SF_INIT_SFFILE_ENVIRONMENT \
({ \
    SfFileEnvironment fEnv; \
    fEnv.header.size = sizeof(SfFileEnvironment); \
    fEnv.header.type = SF_ENVIRONMENT_TYPE_FILE; \
    fEnv.processContext = SF_INIT_SFEXECUTION_ENVIRONMENT; \
    fEnv.pFileName = NULL; \
    fEnv.inode = 0; \
    fEnv; \
})

/**
****************************************************************************************************
* @brief Init process environment
****************************************************************************************************
*/
#define SF_INIT_SFPROCESS_ENVIRONMENT \
({ \
    SfProcessEnvironment pEnv; \
    pEnv.header.size = sizeof(SfProcessEnvironment); \
    pEnv.header.type = SF_ENVIRONMENT_TYPE_PROCESS; \
    pEnv.processContext = SF_INIT_SFEXECUTION_ENVIRONMENT; \
    pEnv.pProcessName = NULL; \
    pEnv.processImageId = 0; \
    pEnv; \
})

/**
****************************************************************************************************
* @brief Init network environment
****************************************************************************************************
*/
#define SF_INIT_SFNETWORK_ENVIRONMENT \
({ \
    SfNetworkEnvironment nEnv; \
    nEnv.header.size = sizeof(SfNetworkEnvironment); \
    nEnv.header.type = SF_ENVIRONMENT_TYPE_NETWORK; \
    nEnv.processContext = SF_INIT_SFEXECUTION_ENVIRONMENT; \
    nEnv.addr = 0; \
    nEnv.port = 0; \
    nEnv; \
})

/**
****************************************************************************************************
* @brief Init mmap environment
****************************************************************************************************
*/
#define SF_INIT_SFMMAP_ENVIRONMENT \
({ \
    SfMmapEnvironment mmapEnvironment; \
    mmapEnvironment.header.size = sizeof(SfMmapEnvironment); \
    mmapEnvironment.header.type = SF_ENVIRONMENT_TYPE_NETWORK; \
    mmapEnvironment.processContext = SF_INIT_SFEXECUTION_ENVIRONMENT; \
    mmapEnvironment.pLibraryName = NULL; \
    mmapEnvironment.inode = 0; \
    mmapEnvironment; \
})

/**
****************************************************************************************************
* @typedef SF_ENVIRONMENT_TYPE
* @brief Environment type definition. Operation mapped to corresponding resource type presented
*   in the system
****************************************************************************************************
*/
typedef enum
{
    SF_ENVIRONMENT_TYPE_PROCESS = 0,        ///< Process environment
    SF_ENVIRONMENT_TYPE_FILE = 1,           ///< File environment
    SF_ENVIRONMENT_TYPE_NETWORK = 2,        ///< Network environment
    SF_ENVIRONMENT_TYPE_MMAP = 3,           ///< mmap() system call environment
    SF_ENVIRONMENT_TYPE_MAX = SF_ENVIRONMENT_TYPE_MMAP
} SF_ENVIRONMENT_TYPE;

/**
****************************************************************************************************
* @struct SfExecutionEnvironmentInfo
* @brief Information about exection environement
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
#if defined(SF_LEVEL_KERNEL)
    Char* pBuffer;          ///< Buffer to hold process name (in kernel space)
#endif
    Char* pProcessName;     ///< Process name
    Uint32 processId;       ///< Process ID
    Int32 sysCallResult;    ///< Action result
    Uint64 timeStamp;       ///< Time in miliseconds from system start when action was done
} SfExecutionEnvironmentInfo;

/**
****************************************************************************************************
* @struct SfFileEnvironment
* @brief File type operation environment
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader        header;             ///< Structure header

    SfExecutionEnvironmentInfo processContext;  ///< Operation execution environment
#if defined(SF_LEVEL_KERNEL)
    Char* pBuffer;                              ///< File name buffer (in kernel space)
#endif
    Char* pFileName;                            ///< File name
    Uint64 inode;                               ///< Inode of the file
} SfFileEnvironment;

/**
****************************************************************************************************
* @struct SfProcessEnvironment
* @brief Process type operation environment
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;                    ///< Structure header

    SfExecutionEnvironmentInfo processContext;  ///< Operation execution environment
#if defined(SF_LEVEL_KERNEL)
    Char* pBuffer;                              ///< Process name buffer (in kernel space)
#endif
    Char* pProcessName;                         ///< Process name going to be executed
    Uint64 processImageId;                      ///< Inode number of the process image (ELF file)
} SfProcessEnvironment;

/**
****************************************************************************************************
* @struct SfNetworkEnvironment
* @brief Network type operation environment
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;                    ///< Structure header

    SfExecutionEnvironmentInfo processContext;  ///< Operation execution environment
    Uint32 addr;                                ///< IPv4 address
    Uint16 port;                                ///< TCP/UDP port ??
} SfNetworkEnvironment;

/**
****************************************************************************************************
* @struct SfMmapEnvironment
* @brief Environment for mmap() system call
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
    SfProtocolHeader header;                    ///< Header

    SfExecutionEnvironmentInfo processContext;  ///< Operation execution environment
#if defined(SF_LEVEL_KERNEL)
    Char* pBuffer;                              ///< Buffer to hold called library name
#endif
    Char* pLibraryName;                         ///< Called library name
    Uint64 inode;                               ///< Inode of the library file
} SfMmapEnvironment;

#if defined(SF_LEVEL_KERNEL)

// Forward declaration of the linux kernel process context
struct path;
struct task_struct;

/**
****************************************************************************************************
* @brief                        Fill execution environment(calling process information)
* @param [in,out] pEnvironment  Environment to be filled
* @param [in] pProcessContext   Calling process
* @param [in] time              Event time
* @param [in] result            Event result
* @return                       SF_STATUS_BAD_ARG in case if parameters was invalid,
*                               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillExecutionEnvironment( SfExecutionEnvironmentInfo* pEnvironment,
                                             struct task_struct* pProcessContext, Uint64 time,
                                             Int32 result );

/**
****************************************************************************************************
* @brief                        Fill file environment from operation parameters
* @param [in,out] pEnvironment  Pointer to the file environment
* @param [in] pArgs             LSM file_open arguments
* @return                       SF_STATUS_BAD_ARG in case if parameters was invalid,
*                               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillFileEnvironment( SfFileEnvironment* pEnvironment,
                                        const SfOperationFileOpen* pArgs );

/**
****************************************************************************************************
* @brief                        Fill process environment from operation parameters
* @param [in,out] pEnvironment  Pointer to the process environment
* @param [in] pArgs             LSM bprm_check_security arguments
* @return                       SF_STATUS_BAD_ARG in case if parameters was invalid,
*                               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillProcessEnvironment( SfProcessEnvironment* pEnvironment,
                                           const SfOperationBprmCheckSecurity* pArgs );

/**
****************************************************************************************************
* @brief                        Fill network environment from operation parameters
* @param [in,out] pEnvironment  Pointer to the network environment
* @param [in] pArgs             LSM socket_connect arguments
* @return                       SF_STATUS_BAD_ARG in case if parameters was invalid,
*                               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillNetworkEnvironment( SfNetworkEnvironment* pEnvironment,
                                           const SfOperationSocketConnect* pArgs );

/**
****************************************************************************************************
* @brief                        Fill mmap() system call environment from operation parameters
* @param [in,out] pEnvironment  Pointer to the mmap() environment
* @param [in] pArgs             LSM security_mmap_file() arguments
* @return                       SF_STATUS_BAD_ARG in case if parameters was invalid,
*                               SF_STATUS_OK on success, SF_STATUS_FAIL otherwise
****************************************************************************************************
*/
SF_STATUS SFAPI SfdFillMmapEnvironment( SfMmapEnvironment* pEnvironment,
                                        const SfOperationFileMmap* pArgs );

/**
****************************************************************************************************
* @brief                        Construct absolute file name by path object
* @param [in] path              Path associated with file
* @param [out] pName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByPath( struct path* path, char** pName );

/**
****************************************************************************************************
* @brief                        Construct absolute file name by file object
* @param [in] pFile             Pointer to the file structure
* @param [out] pName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SfConstructAbsoluteFileNameByFile( struct file* const pFile, Char** const pName );

/**
****************************************************************************************************
* @brief                        Construct absolute file name by process context
* @param [in] pProcessContext   Pointer to the process context object
* @param [out] pName            Pointer to the file name
* @return                       Buffer with file name on success, NULL on failure
* @warning                      Result of this function should be freed later (sf_free MUST be used)
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByTask( const struct task_struct* pProcessContext,
                                               Char** pName );

#endif  /* SF_LEVEL_KERNEL */

/**
****************************************************************************************************
* @brief                        Destroy SF file environment
* @param [in] pEnv              SF file environment
* @return
****************************************************************************************************
*/
void SfDestroyFileEnvironment( SfFileEnvironment* pEnv );

/**
****************************************************************************************************
* @brief                        Destroy SF process environment
* @param [in] pEnv              SF process environment
* @return
****************************************************************************************************
*/
void SfDestroyProcessEnvironment( SfProcessEnvironment* pEnv );

/**
****************************************************************************************************
* @brief                        Destroy SF network environment
* @param [in] pEnv              SF network environment
* @return
****************************************************************************************************
*/
void SfDestroyNetworkEnvironment( SfNetworkEnvironment* pEnv );

/**
****************************************************************************************************
* @brief                        Destroy SF mmap() environment
* @param [in] pEnv              SF mmap() environment
* @return
****************************************************************************************************
*/
void SfDestroyMmapEnvironment( SfMmapEnvironment* pEnv );

/**
****************************************************************************************************
* @brief                        Destroy SF environment of different type
* @param [in] pEnv              SF environment header
* @return
****************************************************************************************************
*/
void SfDestroyEnvironment( SfProtocolHeader* pHeader );

#ifdef __cplusplus
}
#endif /* !__cplusplus */

#endif  /* !_SF_ENVIRONMENT_FORMAT_H_ */
