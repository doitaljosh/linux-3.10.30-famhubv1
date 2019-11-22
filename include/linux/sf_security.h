/**
****************************************************************************************************
* @file sf_security.h
* @brief Security framework [SF] kernel security operations definition
* @author Maksym Koshel (m.koshel@samsung.com)
* @date Created Sep 20, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#ifndef _SF_SECURITY_H_
#define _SF_SECURITY_H_

// forward declarations
struct file;
struct linux_binprm;
struct cred;
struct socket;
struct sockaddr;


/*
* Enable File OPEN operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_OPEN)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_socket_create function.
* @see sf_security_socket_create
****************************************************************************************************
*/
typedef int (*sf_open_fptr)(struct file* pFile, const struct cred* pCredentials);

/**
****************************************************************************************************
* @brief Process open file event and sends it to user space listeners
* @param [in] pFile Pointer to the file to be processed
* @param [in] pCredentials Pointer to the file credentials
* @warning In future return value may be changed in case if security permissions will be added
* @return 0 on success, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_file_open(struct file* pFile, const struct cred* pCredentials);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_file_open(struct file* pFile, const struct cred* pCredentials)
{
	return 0;
}

#endif

/*
* Enable File EXEC operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_EXEC)
	
/**
****************************************************************************************************
* @brief Function type that cover sf_security_bprm_check function.
* @see sf_security_bprm_check
****************************************************************************************************
*/
typedef int (*sf_exec_fptr)(struct linux_binprm* pBinaryParameters);

/**
****************************************************************************************************
* @brief Verify process digital signature that is going to be executed
* @param [in] pBinaryParameters Pointer to the process binary parameters
* @return 0 in case if verification passed successfully, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_bprm_check(struct linux_binprm* pBinaryParameters);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_bprm_check(struct linux_binprm* pBinaryParameters)
{
	return 0;
}

#endif


/*
* Enable File MMAP operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_MMAP)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_mmap_file security function.
* @see sf_security_mmap_file
****************************************************************************************************
*/
typedef int (*sf_mmap_fptr)(struct file* pFile, unsigned long prot,
	unsigned long flags);

/**
****************************************************************************************************
* @brief Check permissions for a memory map operation. This function interested in dynamic library
*	loading. This function will call routine for digital signature verification.
* @param [in] pFile Pointer to the file to be mapped
* @param [in] prot Protection that will be applyied by the kernel
* @param [in] flags Contains the operational flags
* @return 0 in case if verification passed successfully, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_mmap_file(struct file* pFile, unsigned long prot, unsigned long flags);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
*	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_mmap_file(struct file* pFile, unsigned long prot,
	unsigned long flags)
{
	return 0;
}

#endif

/*
* Enable Kernel module load operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATCHER_LOAD_KERNEL_MODULE)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_kernel_module_from_file security function.
* @see sf_security_kernel_module_from_file
****************************************************************************************************
*/
typedef int (*sf_insmod_fptr)(struct file* pFile);

/**
****************************************************************************************************
* @brief Check permissions for kernel module loading into kernel. This function call routine for
*	digital signature verification.
* @param [in] pFile Pointer to the file represents kernel module
* @return 0 in case if verification passed successfully, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_kernel_module_from_file(struct file* pFile);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
*	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_kernel_module_from_file(struct file* pFile)
{
	return 0;
}

#endif

/*
* Enable Socket create operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_SOCKET)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_file_open function.
* @see sf_security_file_open
****************************************************************************************************
*/
typedef int (*sf_socket_fptr)(int family, int type, int protocol, int kernel);

/**
****************************************************************************************************
* @brief Process socket creation event and sends it to user space listeners
* @param [in] family Contains requested protocol family
* @param [in] type Contains requested communication type
* @param [in] protocol Contains requested protocol type
* @param [in] kernel Contains value about socket level (1 for kernel socket, 0 for user space)
* @return 0 on success, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_socket_create(int family, int type, int protocol, int kernel);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_socket_create(int family, int type, int protocol, int kernel)
{
	return 0;
}

#endif

/*
* Enable Socket bind operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_BIND)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_socket_bind function.
* @see sf_security_socket_bind
****************************************************************************************************
*/
typedef int (*sf_bind_fptr)(struct socket* pSocket, struct sockaddr* pAddress,
	int addrSize);

/**
****************************************************************************************************
* @brief Process socket bind event and sends it to user space listeners
* @param [in] pSocket Pointer to the socket for bind
* @param [in] pAddress Pointer to the socket address structure
* @param [in] addrSize Size in bytes of the 'pAddress' parameter
* @return 0 on success, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_socket_bind(struct socket* pSocket, struct sockaddr* pAddress, int addrSize);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_socket_bind(struct socket* pSocket, struct sockaddr* pAddress,
	int addrSize)
{
	return 0;
}

#endif

/*
* Enable Socket connect operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_CONNECT)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_socket_connect function.
* @see sf_security_socket_connect
****************************************************************************************************
*/
typedef int (*sf_connect_fptr)(struct socket* pSocket, struct sockaddr* pAddress, int addrSize);

/**
****************************************************************************************************
* @brief Process socket connect event and sends it to user space listeners. In case of 'pAddress'
*	was found in block list error will be returned and connection will be blocked.
* @param [in] pSocket Pointer to the socket for sonnection
* @param [in] pAddress Pointer to the socket address structure
* @param [in] addrSize Size in bytes of the 'pAddress' parameter
* @warning This function may block connection
* @return 0 if connection is allowed, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_socket_connect(struct socket* pSocket, struct sockaddr* pAddress, int addrSize);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_socket_connect(struct socket* pSocket, struct sockaddr* pAddress,
	int addrlen)
{
	return 0;
}

#endif

/*
* Enable Socket accept operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_ACCEPT)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_socket_accept function.
* @see sf_security_socket_accept
****************************************************************************************************
*/
typedef int (*sf_accept_fptr)(struct socket* pSocket, struct socket* pNewSocket);

/**
****************************************************************************************************
* @brief Process socket accept event and sends it to user space listeners.
* @param [in] pSocket Pointer to the listening socket structure
* @param [in] pNewSocket Pointer to the newly created server socket structure
* @return 0 on success, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_socket_accept(struct socket* pSocket, struct socket* pNewSocket);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_socket_accept(struct socket *sock, struct socket *newsock)
{
	return 0;
}

#endif

/*
* Enable Socket listen operation hook
*/
#if defined(CONFIG_SECURITY_SFD) && defined(CONFIG_SECURITY_SFD_DISPATHCER_LISTEN)

/**
****************************************************************************************************
* @brief Function type that cover sf_security_socket_listen function.
* @see sf_security_socket_listen
****************************************************************************************************
*/
typedef int (*sf_listen)(struct socket* pSocket, int backLog);

/**
****************************************************************************************************
* @brief Process socket listen event and sends it to user space listeners.
* @param [in] pSocket Pointer to the socket structure
* @param [in] backLog Contains the maximum length for the pending connection queue
* @return 0 on success, non 0 - otherwise
****************************************************************************************************
*/
int sf_security_socket_listen(struct socket* pSocket, int backLog);

#else

/**
****************************************************************************************************
* @brief This function used by the kernel when CONFIG_SFD_SECURITY disabled to prevent compilation
*	fail. static inline is used for possible compiler optimization (branch prediction, cache flush,
	etc)
* @note This is empty stub
****************************************************************************************************
*/
static inline int sf_security_socket_listen(struct socket* pSocket, int backLog)
{
	return 0;
}

#endif

#endif /* !_SF_SECURITY_H_ */
