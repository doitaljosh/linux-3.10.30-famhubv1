#ifndef __SDP_HAWKO_PCIE_H
#define __SDP_HAWKO_PCIE_H


#include "pcie-sdp.h"

#define PCI_VENDOR_ID_HAWKO 0x144d
#define PCI_DEVICE_ID_HAWKO 0xa63a

#define BAR0_SIZE 32*1024*1024

int sdp_pcie_get_outbound_atubase(void);
int sdp_pcie_get_outbound1_atubase(void);
int sdp_pcie_get_outbound_targetbase(void);
int sdp_pcie_get_inbound_targetbase(void);
int sdp_pcie_get_ep_slv(void);
int sdp_pcie_get_ep_slv_size(void);
int sdp_pcie_get_ep_mem(void);
int sdp_pcie_get_ep_mem_size(void);
#endif

