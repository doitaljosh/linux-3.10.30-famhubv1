
#ifndef _SF_PROTOCOL_HEADER_H_
#define _SF_PROTOCOL_HEADER_H_

#include <uapi/linux/sf/core/SfCore.h>

/**
****************************************************************************************************
* @struct SfProtocolHeader
* @brief Protocol header used in all protocol related structures
****************************************************************************************************
*/
typedef struct __attribute__((__packed__))
{
	Uint size; ///< Owner structure size

	/**
	* @warning For type field should be used following enumerations: SF_OPERATION_TYPE,
	*	SF_PACKET_TYPE and SF_ENVIRONMENT_TYPE.
	* @see SF_PACKET_TYPE
	* @see SF_OPERATION_TYPE
	* @see SF_ENVIRONMENT_TYPE
	*/
	Uint type : 32;

} SfProtocolHeader;

#endif	/* _SF_PROTOCOL_HEADER_H_ */
