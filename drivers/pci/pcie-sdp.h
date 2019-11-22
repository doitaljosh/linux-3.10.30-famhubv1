#ifndef __SDP_PCIE_H
#define __SDP_PCIE_H

//--------------------------------------------------------------------------//
// CDM: Application Specific Regsiters (Offset)								//
//--------------------------------------------------------------------------//
unsigned int VENDER_ID						  = 0x144D;
unsigned int DEVICE_ID						  = 0xA63A;
unsigned int SUB_VENDER_ID					= 0x144D;
unsigned int SUB_DEVICE_ID					= 0xA63A;

unsigned int PTR_PM_CAP   					= 0x040;
unsigned int PTR_MSI_CAP  					= 0x050;
unsigned int PTR_PCIE_CAP 					= 0x070;

unsigned int PTR_AER_CAP  					= 0x100; 
unsigned int PTR_VC_CAP   					= 0x148;		// changed by IP_version 
unsigned int PTR_PL_REG   					= 0x700; 

//--------------------------------------------------------------------------//
// ELBI: Application Specific Regsiters (Offset)							//
//--------------------------------------------------------------------------//

unsigned int PCIE_MAIL_BOX0             	= 0x0F8;
unsigned int PCIE_MAIL_BOX1             	= 0x0FC;
unsigned int PCIE_MAIL_BOX2             	= 0x100;
unsigned int PCIE_MAIL_BOX3             	= 0x104;
unsigned int PCIE_INTMSI                	= 0x108;
unsigned int PCIE_DOORBELL              	= 0x10C;
unsigned int PCIE_USER_CONFIG0          	= 0x200;
unsigned int PCIE_USER_CONFIG1          	= 0x204;
unsigned int PCIE_USER_CONFIG2          	= 0x208;
unsigned int PCIE_USER_CONFIG3          	= 0x20C;
unsigned int PCIE_USER_CONFIG4          	= 0x210;
unsigned int PCIE_USER_CONFIG5          	= 0x214;
unsigned int PCIE_USER_CONFIG6          	= 0x218;
unsigned int PCIE_USER_CONFIG7          	= 0x21C;
unsigned int PCIE_USER_CONFIG8          	= 0x220;
unsigned int PCIE_USER_CONFIG9          	= 0x224;
unsigned int PCIE_USER_CONFIG10          	= 0x228;
unsigned int PCIE_USER_CONFIG11          	= 0x22C;
unsigned int PCIE_USER_ONEPULSE_CONFIG0 	= 0x230;
unsigned int PCIE_USER_ONEPULSE_CONFIG1 	= 0x234;

unsigned int PCIE_MSTR_CTRL             	= 0x240;
unsigned int PCIE_MSTR_BCTRL            	= 0x244;
unsigned int PCIE_MSTR_RCTRL            	= 0x248;

unsigned int PCIE_MSTR_AWCTRL_L           = 0x250;
unsigned int PCIE_MSTR_AWCTRL_H           = 0x254;
unsigned int PCIE_MSTR_AWCTRL_DMA_SEL     = 0x258;

unsigned int PCIE_MSTR_ARCTRL_L           = 0x260;
unsigned int PCIE_MSTR_ARCTRL_H           = 0x264;
unsigned int PCIE_MSTR_ARCTRL_DMA_SEL     = 0x268;
unsigned int PCIE_SLV_CTRL              	= 0x26C;

unsigned int PCIE_SLV_AWCTRL            	= 0x270;
unsigned int PCIE_SLV_ARCTRL            	= 0x274;
unsigned int PCIE_SLV_BCTRL             	= 0x278;
unsigned int PCIE_SLV_RCTRL             	= 0x27C;
unsigned int PCIE_USER_CTRL0            	= 0x280;
unsigned int PCIE_USER_CTRL1            	= 0x284;
unsigned int PCIE_USER_CTRL2            	= 0x288;
unsigned int PCIE_USER_CTRL3            	= 0x28C;
unsigned int PCIE_USER_CTRL4            	= 0x290;
unsigned int PCIE_USER_CTRL5            	= 0x294;
unsigned int PCIE_USER_ONEPULSE_CTRL0   	= 0x2A0;

unsigned int BAR_NONPREFETCHABLE 			    = ((0x0)<<(3));
unsigned int BAR_PREFETCHABLE    			    = ((0x1)<<(3));
unsigned int BAR_MEMORY_SPACE    			    = ((0x0)<<(0));
unsigned int BAR_IO_SPACE        			    = ((0x1)<<(0));
unsigned int BAR_32BIT_ADDR      			    = ((0x0)<<(2));
unsigned int BAR_64BIT_ADDR      			    = ((0x1)<<(2));


#define ELBI_SLVACTL_MEM	(0x0)
#define ELBI_SLVACTL_IO		(0x2)
#define ELBI_SLVACTL_CFG0	(0x4)
#define ELBI_SLVACTL_CFG1	(0x5)

//--------------------------------------------------//
//	PCIe PHY										//
//--------------------------------------------------//
// CDM
#define	PCIe_DBI_BASE					0x111A_0000
#define PCIe_PHY_STATUS_REG		 (0x700 + 0x110)
#define	PCIe_PHY_CTRL_REG	 (0x700 + 0x114)

// ELBI
#define	PCIe_ELBI_BASE				PCIe_DBI_BASE	+ 0x2000

#define USER_CTRL_0						PCIe_ELBI_BASE	+ (0x180)
#define USER_CTRL_1						PCIe_ELBI_BASE	+ (0x184)
#define USER_CTRL_2						PCIe_ELBI_BASE	+ (0x188)
#define USER_CTRL_3						PCIe_ELBI_BASE	+ (0x18C)

//--------------------------------------------------//
//	PCIe MODE										//
//--------------------------------------------------//
#define	PCIe_SLV1_BASE				0x20000000

#define BASE_ADDR_PCI_PHY			0x11160000  
#define BASE_ADDR_PCI_GPR			0x11170000

unsigned int BAR_PCIe_CDM   = 0x111A0000;
unsigned int BAR_PCIe_ELBI  = 0x111A2000;

#define	ID_RC	0                         
#define ID_EP	1                         
                                        
#define	SPEED_GEN1	0                   
#define SPEED_GEN2	1                   
                   
#define	MAX_PAYLOAD_IS_512              
                                        
#define TESTCASE_CONFIG0						0       
#define TESTCASE_IO_MEM_REQUEST			1   
#define TESTCASE_INTERRUPT_EP2RC		2   
#define TESTCASE_IO_EP2RC						3       
#define TESTCASE_MEM_THROUGHPUT			4   
#define TESTCASE_MEMCOPY_THROUGHPUT	5 
#define TESTCASE_MEM_REQUEST_ELBI		6   

#define BAR_NONPREFETCHABLE		((0x0)<<(3))
#define BAR_PREFETCHABLE			((0x1)<<(3))
#define BAR_MEMORY_SPACE			((0x0)<<(0))
#define BAR_IO_SPACE					((0x1)<<(0))
#define BAR_32BIT_ADDR				((0x0)<<(2))
#define BAR_64BIT_ADDR				((0x1)<<(2))


#define DT_FLAGS_TO_TYPE(flags)       (((flags) >> 24) & 0x03)
#define	   DT_TYPE_CON				  0x0
#define    DT_TYPE_IO                 0x1
#define    DT_TYPE_MEM32              0x2
//--------------------------------------------------//
//	IATU   										       //
//--------------------------------------------------//

#define PCIE_ATU_VIEWPORT		0x900
#define PCIE_ATU_REGION_INBOUND		(0x1 << 31)
#define PCIE_ATU_REGION_OUTBOUND	(0x0 << 31)
#define PCIE_ATU_REGION_INDEX1		(0x1 << 0)
#define PCIE_ATU_REGION_INDEX0		(0x0 << 0)
#define PCIE_ATU_CR1			0x904
#define PCIE_ATU_TYPE_MEM		(0x0 << 0)
#define PCIE_ATU_TYPE_IO		(0x2 << 0)
#define PCIE_ATU_TYPE_CFG0		(0x4 << 0)
#define PCIE_ATU_TYPE_CFG1		(0x5 << 0)
#define PCIE_ATU_CR2			0x908
#define PCIE_ATU_ENABLE			(0x1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE	(0x1 << 30)
#define PCIE_ATU_LOWER_BASE		0x90C
#define PCIE_ATU_UPPER_BASE		0x910
#define PCIE_ATU_LIMIT			0x914
#define PCIE_ATU_LOWER_TARGET		0x918
#define PCIE_ATU_BUS(x)			(((x) & 0xff) << 24)
#define PCIE_ATU_DEV(x)			(((x) & 0x1f) << 19)
#define PCIE_ATU_FUNC(x)		(((x) & 0x7) << 16)
#define PCIE_ATU_UPPER_TARGET		0x91C

#endif /* __PCIE_SDP_H */
