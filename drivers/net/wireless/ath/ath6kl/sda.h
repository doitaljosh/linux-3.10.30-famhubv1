
#ifndef SDA_H
#define SDA_H

/* samsung direct audio prototype */

typedef struct SDA_Descriptor
{
	unsigned char m_ReadyToCopy;
	
	unsigned int m_PayloadSize;
	
	unsigned int m_TimeStamp;
}__attribute__((packed)) SDA_Descriptor_t;


typedef struct SDA_HeadRoom
{
	unsigned char m_DestAddress[6];
	
	unsigned char m_SourceAddress[6];
	
	unsigned short m_PacketType;
}__attribute__((packed)) SDA_HeadRoom_t;


typedef struct
{
	SDA_Descriptor_t   m_Descriptor;
	
	unsigned char m_Dummy[50 - sizeof(struct SDA_HeadRoom)];
	
	SDA_HeadRoom_t  m_Headroom;
}__attribute__((packed)) SDA_Header_t;



/**
 * @brief tsf�� wifi module�� ���� ����
 * @remarks
 * @param pTsf : tsf 
 * @return none 
 * @see 
 */
void SDA_GetTSF(unsigned int *pTsf);




/**
 * @brief wifi module�� tx�� ���ؼ� ����� buffer ������ �˷���.
 * @brief send the buffer info to wifi module for tx.
 * @remarks
 * @param BufferId direct audio system���� audio input buffer�� �ִ� 5���� ������. buffer�� id��. 
 * @param BufferId is the mazimum 5. 
 * @param pBufferTotal : buffer address
 * @param BufferTotalSize : buffer size
 * @param BufferUnitSize : each unit size (descriptor + headroom + audio data size)
 * @param HeadroomSize : head room size
 * @return none 
 * @see 
 */
void SDA_setSharedMemory4Send(unsigned int BufferId, unsigned char *pBufferTotal, unsigned int BufferTotalSize, unsigned int BufferUnitSize, unsigned int HeadroomSize);





/**
 * @brief wifi module�� rx�� ���ؼ� ����� buffer ������ �˷���.
 * @brief send the buffer info to wifi module for rx.
 * @remarks
 * @param pBufferTotal : buffer address
 * @param BufferTotalSize : buffer size
 * @param BufferUnitSize : each unit size (descriptor + headroom + audio data size)
 * @param HeadroomSize : head room size
 * @return none 
 * @see 
 */
void SDA_setSharedMemory4Recv(unsigned char *pBufferTotal, unsigned int BufferTotalSize, unsigned int BufferUnitSize, unsigned int HeadroomSize);






/**
 * @brief ������ data�� wifi module�� ������. 
 * @send the brief data data to wifi module (dsp --> wifi module)
 * @remarks
 * @param BufferId direct audio system���� audio input buffer�� �ִ� 5���� ������. buffer�� id��. 
 * @param on BufferId direct audio system, the maximum of audio input buffer is 5. buffer�� id��. 
 * @param pBuffer : buffer address
 * @param BufferSize : buffer size
 * @return none 
 * @see 
 */
void SDA_function4Send(unsigned int BufferId, unsigned char *pBuffer, unsigned int BufferSize);




/**
 * @brief wifi module���� data�� ������ �Ŀ� callbackȣ�� (ack)
 * @brief after sending data on wifi module, call the callback(ack) (wifi module --> dsp)
 * @remarks
 * @param BufferId direct audio system���� audio input buffer�� �ִ� 5���� ������. buffer�� id��. 
 * @param on BufferId direct audio system, the maximum of audio input buffer is 5.
 * @param pBuffer : buffer address
 * @param BufferSize : buffer size
 * @return none 
 * @see 
 */
typedef void (*SDA_SendDoneCallBack)(unsigned int BufferId, unsigned char *pBuffer, unsigned int BufferSize);

void SDA_registerCallback4SendDone(SDA_SendDoneCallBack pCallback);







/**
 * @brief wifi module���� ���� ���� �����͸� soc�� ������. 
 * @brief on wifi module, send the receiving data to soc (wifi module --> dsp)
 * @remarks
 * @param pBuffer : buffer address
 * @param BufferSize : buffer size
 * @return none 
 * @see 
 */
typedef void (*SDA_RecvCallBack)(unsigned char *pBuffer, unsigned int BufferSize);

void SDA_registerCallback4Recv(SDA_RecvCallBack pCallback);





/**
 * @brief soc���� ���� ���� �����͸� ó���Ŀ� wifi module�� �˸�. (ack)
 * @brief after finishing to received data on soc, notifying to wifi module(ack)
 * @remarks
 * @param pBuffer : buffer address
 * @param BufferSize : buffer size
 * @return none 
 * @see 
 */
void SDA_function4RecvDone(unsigned char *pBuffer, unsigned int BufferSize);


	

#endif // SDA_H
