#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/mach/pci.h>
#include <linux/platform_device.h>

#include <asm/irq.h>

#include "pcie-sdp.h"

/* arm dependent */
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

#define PCIE_EP 0
struct sdp_pcie {
	struct platform_device *pdev;
	void __iomem		*phy_base;
	void __iomem		*gpr_base;
	u32			p_cfg0_base;
	u32			p_cfg1_base;
	u32 		p_mem_base;
	int mem_size;

	void __iomem* v_cfg0_base;
	int cfg0_size;
	void __iomem* v_cfg1_base;
	int cfg1_size;
	int root_bus_nr;

	int irq;

	struct resource mem;
	struct resource mem_non;
	struct resource busn;
//	struct resource busn;
	struct device *dev;
	struct clk		*clk;
	struct clk		*gpr_clk;
	struct clk		*clk_mask;
};
static struct sdp_pcie_phy
{
	u32 phy;
	u32 gpr;	
}  sdp_pcie_phy_;
struct sdp_pcie_base
{
	void __iomem		*cdm_vbase;
	void __iomem		*elbi_vbase;
	struct resource 	*cdm_pbase;
	struct resource 	*elbi_pbase;

}SDP_PCIE_BASE_T;
static u32 bar0;

static DEFINE_SPINLOCK(sdp_lock);
static inline struct sdp_pcie *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

static int dw_pcie_cdm_writel(void __iomem *addr, int where, int size, u32 val)
{
	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr + (where & 2));
	else if (size == 1)
		writeb(val, addr + (where & 3));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;

}
int dw_pcie_cdm_readl(void __iomem *addr, int where, int size, u32 *val)
{
	*val = readl(addr);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;
	else if (size != 4)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;

}
void sdp_cdm_writel(u32 val, u32 reg)
{
	writel(val, SDP_PCIE_BASE_T.cdm_vbase + reg);
	readl(SDP_PCIE_BASE_T.cdm_vbase + reg);
}
EXPORT_SYMBOL(sdp_cdm_writel);
u32 sdp_cdm_readl(u32 reg)
{
	return readl(SDP_PCIE_BASE_T.cdm_vbase + reg);
}
EXPORT_SYMBOL(sdp_cdm_readl);
void sdp_elbi_writel(u32 val, u32 reg)
{
	writel(val, SDP_PCIE_BASE_T.elbi_vbase + reg);
	readl(SDP_PCIE_BASE_T.elbi_vbase + reg);
}
EXPORT_SYMBOL(sdp_elbi_writel);
u32 sdp_elbi_readl(u32 reg)
{
	return readl(SDP_PCIE_BASE_T.elbi_vbase + reg);
}
EXPORT_SYMBOL(sdp_elbi_readl);
static inline void sdp_phy_writel(struct sdp_pcie *pcie, u32 val, u32 reg)
{
	writel(val, pcie->phy_base + reg);
}

static inline u32 sdp_phy_readl(struct sdp_pcie *pcie, u32 reg)
{
	return readl(pcie->phy_base + reg);
}
static inline void sdp_gpr_writel(struct sdp_pcie *pcie, u32 val, u32 reg)
{
	writel(val, pcie->gpr_base + reg);
}

static inline u32 sdp_gpr_readl(struct sdp_pcie *pcie, u32 reg)
{
	return readl(pcie->gpr_base + reg);
}


#define  PCIE_CONF_REG(r)	((r) & 0xff)
#define  PCIE_CONF_BUS(b)		(((b) & 0xff) << 16)
#define  PCIE_CONF_DEV(d)		(((d) & 0x1f) << 11)
#define  PCIE_CONF_FUNC(f)		(((f) & 0x7) << 8)


int dw_pcie_cfg_read(void __iomem *addr, int where, int size, u32 *val)
{
	*val = readl(addr);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;
	else if (size != 4)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}
static int pcie_prog_viewport_cfg0(struct sdp_pcie *pcie, u32 busdev)
{
	//dev_info(&pcie->pdev->dev, "cfg0... %x\n",busdev);

	sdp_elbi_writel(ELBI_SLVACTL_CFG0,PCIE_SLV_AWCTRL); //config0 space..
	sdp_elbi_writel(ELBI_SLVACTL_CFG0,PCIE_SLV_ARCTRL);
	
	//CFG0
	sdp_cdm_writel(PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1,PCIE_ATU_VIEWPORT);	// set [31]: outbound=0, region 0 
	sdp_cdm_writel(PCIE_ATU_TYPE_CFG0,PCIE_ATU_CR1);	// the type of the region to be CFG(5), I/O(2), MEM(0)
	sdp_cdm_writel(pcie->p_cfg0_base,PCIE_ATU_LOWER_BASE); // set the Lower Base Address
	sdp_cdm_writel(0x00000000,PCIE_ATU_UPPER_BASE);	// set the Upper Base Address
	sdp_cdm_writel(pcie->p_cfg0_base+pcie->cfg0_size-1,PCIE_ATU_LIMIT); // set the Limit Address
	sdp_cdm_writel(busdev,PCIE_ATU_LOWER_TARGET);	// set the Lower Target Address
	sdp_cdm_writel(0x00000000,PCIE_ATU_UPPER_TARGET);	// set the Upper Target Address
	sdp_cdm_writel(PCIE_ATU_ENABLE,PCIE_ATU_CR2);	// set [31] REGION_EN, [27] DMA_BYPASS	

	return 0;

}
static int pcie_prog_viewport_cfg1(struct sdp_pcie *pcie, u32 busdev)
{
	//dev_info(&pcie->pdev->dev, "cfg1... %x\n",busdev);
//	
	sdp_elbi_writel(ELBI_SLVACTL_CFG1,PCIE_SLV_AWCTRL); //config1 space..
	sdp_elbi_writel(ELBI_SLVACTL_CFG1,PCIE_SLV_ARCTRL);
	// outbound Programming
	// CFG 1
//	sdp_cdm_writel(pcie,0,PCIE_ATU_CR2);	// iatu Disable

	sdp_cdm_writel(PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX0,PCIE_ATU_VIEWPORT);	// set [31]: outbound=0, region 0 
	sdp_cdm_writel(PCIE_ATU_TYPE_CFG1,PCIE_ATU_CR1);	// the type of the region to be CFG(5), I/O(2), MEM(0)
	sdp_cdm_writel(pcie->p_cfg1_base,PCIE_ATU_LOWER_BASE); // set the Lower Base Address
	sdp_cdm_writel(0x00000000,PCIE_ATU_UPPER_BASE);	// set the Upper Base Address
	sdp_cdm_writel(pcie->p_cfg1_base+pcie->cfg1_size-1,PCIE_ATU_LIMIT); // set the Limit Address
	sdp_cdm_writel(busdev,PCIE_ATU_LOWER_TARGET);	// set the Lower Target Address
	sdp_cdm_writel(0x00000000,PCIE_ATU_UPPER_TARGET);	// set the Upper Target Address
	sdp_cdm_writel(PCIE_ATU_ENABLE,PCIE_ATU_CR2);	// set [31] REGION_EN, [27] DMA_BYPASS	
	
	return 0;

}
int  pcie_prog_viewport_mem_outbound_RC(u32 bar0)
{
		sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
		sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);
	
		/* Program viewport 0 : OUTBOUND : MEM */
		if(bar0 == 0)return 0;

	//	sdp_cdm_writel(pcie, 0, PCIE_ATU_CR2);	// iatu Disable
		sdp_cdm_writel( PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX0, PCIE_ATU_VIEWPORT);
		sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	//	sdp_cdm_writel(pcie, 0x20000000, PCIE_ATU_LOWER_BASE);
		sdp_cdm_writel( 0x2a000000, PCIE_ATU_LOWER_BASE);
		sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
		sdp_cdm_writel( 0x2bffffff, PCIE_ATU_LIMIT);
		sdp_cdm_writel( bar0, PCIE_ATU_LOWER_TARGET);//EP bar
		sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
		sdp_cdm_writel( PCIE_ATU_ENABLE, PCIE_ATU_CR2);

	return 0;
}
EXPORT_SYMBOL(pcie_prog_viewport_mem_outbound_RC);

int  pcie_prog_viewport_mem1_outbound_RC(u32 bar0)
{
		sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
		sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);

		/* Program viewport 0 : OUTBOUND : MEM */
		if(bar0 == 0)return 0;

	//	sdp_cdm_writel(pcie, 0, PCIE_ATU_CR2);	// iatu Disable
		sdp_cdm_writel( PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
		sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	//	sdp_cdm_writel(pcie, 0x20000000, PCIE_ATU_LOWER_BASE);
		sdp_cdm_writel( 0x2c000000, PCIE_ATU_LOWER_BASE);
		sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
		sdp_cdm_writel( 0x2fffffff, PCIE_ATU_LIMIT);
		sdp_cdm_writel( bar0, PCIE_ATU_LOWER_TARGET);//EP bar
		sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
		sdp_cdm_writel( PCIE_ATU_ENABLE, PCIE_ATU_CR2);

	return 0;
}
EXPORT_SYMBOL(pcie_prog_viewport_mem1_outbound_RC);


static int  pcie_prog_viewport_mem0_inbound_EP(u32 bar0,u32 bar1,u32 phy_addr0,u32 phy_addr1)
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);

	/*                 bar match enable              */
	/* Program viewport 0 : INBOUND : MEM */

	sdp_cdm_writel( PCIE_ATU_REGION_INBOUND | PCIE_ATU_REGION_INDEX0, PCIE_ATU_VIEWPORT);
	sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LOWER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LIMIT);
	sdp_cdm_writel( phy_addr0, PCIE_ATU_LOWER_TARGET);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
	sdp_cdm_writel( PCIE_ATU_ENABLE|PCIE_ATU_BAR_MODE_ENABLE|bar0<<8, PCIE_ATU_CR2);//bar1

	sdp_cdm_writel( PCIE_ATU_REGION_INBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
	sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LOWER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LIMIT);
	sdp_cdm_writel( phy_addr1, PCIE_ATU_LOWER_TARGET);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
	sdp_cdm_writel( PCIE_ATU_ENABLE|PCIE_ATU_BAR_MODE_ENABLE|bar1<<8, PCIE_ATU_CR2);//bar1

	return 0;
}
EXPORT_SYMBOL(pcie_prog_viewport_mem0_inbound_EP);

static int  pcie_prog_viewport_mem1_outbound_EP(u32 bar0)
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);

	/* Program viewport 0 : OUTBOUND : MEM */
	if(bar0 == 0)return 0;

	sdp_cdm_writel( PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
	sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	sdp_cdm_writel( 0x20000000, PCIE_ATU_LOWER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
	sdp_cdm_writel( 0x23ffffff, PCIE_ATU_LIMIT);
	sdp_cdm_writel( bar0, PCIE_ATU_LOWER_TARGET);//EP bar
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
	sdp_cdm_writel( PCIE_ATU_ENABLE, PCIE_ATU_CR2);

	return 0;
}
EXPORT_SYMBOL(pcie_prog_viewport_mem1_outbound_EP);

/*EP access RC MEM space...*/
static int  pcie_prog_viewport_mem1_inbound_RC(u32 phy_addr0) // RC
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);

	/*                 bar match enable              */
	/* Program viewport 0 : INBOUND : MEM */

	sdp_cdm_writel( PCIE_ATU_REGION_INBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
	sdp_cdm_writel( PCIE_ATU_TYPE_MEM, PCIE_ATU_CR1);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LOWER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_BASE);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_LIMIT);
	sdp_cdm_writel( phy_addr0, PCIE_ATU_LOWER_TARGET);
	sdp_cdm_writel( 0x00000000, PCIE_ATU_UPPER_TARGET);
	sdp_cdm_writel( PCIE_ATU_ENABLE|PCIE_ATU_BAR_MODE_ENABLE, PCIE_ATU_CR2);

	return 0;
}

EXPORT_SYMBOL(pcie_prog_viewport_mem1_inbound_RC);

int sdp_pcie_rd_conf (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	int ret = PCIBIOS_SUCCESSFUL;
	unsigned int address, busnr;
	u32 slot, func,busdev;
	unsigned long flags;

	struct sdp_pcie *pcie = sys_to_pcie(bus->sysdata);

	slot = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	busnr = bus->number;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
	 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;
//	printk("config bus num %d slot %d fucn %d devfn %x\n",busnr,slot,func,devfn);

	if (bus->number == pcie->root_bus_nr && PCI_SLOT(devfn) > 0)
	{
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pcie->root_bus_nr && PCI_SLOT(devfn) > 0)
	{	
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	
	spin_lock_irqsave(&sdp_lock, flags);

	if (bus->number != pcie->root_bus_nr)
	{
		if (bus->parent->number == pcie->root_bus_nr) 
		{	pcie_prog_viewport_cfg0(pcie, busdev);
			ret = dw_pcie_cfg_read(pcie->v_cfg0_base+ address, where, size,
					val);
			
		dev_dbg(&pcie->pdev->dev, "PCIe config read %x data %x busdev %x\n",(u32)(pcie->v_cfg0_base+ address),*val,busdev);
		//	pcie_prog_viewport_mem_outbound(pcie);
		} else {
			pcie_prog_viewport_cfg1(pcie, busdev);
			ret = dw_pcie_cfg_read(pcie->v_cfg1_base + address, where, size,
					val);
		//	pcie_prog_viewport_mem_outbound(pcie);
		dev_dbg(&pcie->pdev->dev, "PCIe config read %x data %x busdev %x\n",(u32)(pcie->v_cfg1_base+ address),*val,busdev);
		}
	}else 
		dw_pcie_cdm_readl(SDP_PCIE_BASE_T.cdm_vbase + (where & ~0x3),where,size,val);

	pcie_prog_viewport_mem_outbound_RC(bar0);

	spin_unlock_irqrestore(&sdp_lock, flags);

	return ret;

}
int dw_pcie_cfg_write(void __iomem *addr, int where, int size, u32 val)
{
	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr + (where & 2));
	else if (size == 1)
		writeb(val, addr + (where & 3));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

int sdp_pcie_wr_conf (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	int ret = PCIBIOS_SUCCESSFUL;
	unsigned int address, busnr;
	unsigned long flags;
	u32 slot, func,busdev;
	struct sdp_pcie *pcie = sys_to_pcie(bus->sysdata);


	slot = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	busnr = bus->number;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
	 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;
//	printk("config bus num %d slot %d fucn %d devfn %x\n",busnr,slot,func,devfn);

	if (bus->number == pcie->root_bus_nr && PCI_SLOT(devfn) > 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pcie->root_bus_nr && PCI_SLOT(devfn) > 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&sdp_lock, flags);
	if (bus->number != pcie->root_bus_nr)
	{
		if (bus->parent->number == pcie->root_bus_nr) 
		{	pcie_prog_viewport_cfg0(pcie, busdev);
			ret = dw_pcie_cfg_write(pcie->v_cfg0_base+ address, where, size,val);
			
		dev_dbg(&pcie->pdev->dev, "PCIe config wirte %x data %x\n",(u32)(pcie->v_cfg0_base+ address),val);
			//pcie_prog_viewport_mem_outbound(pcie);
		} 
		else{
			pcie_prog_viewport_cfg1(pcie, busdev);
			ret = dw_pcie_cfg_write(pcie->v_cfg1_base + address, where, size,
					val);
		//	pcie_prog_viewport_mem_outbound(pcie);
		dev_dbg(&pcie->pdev->dev, "PCIe config wirte %x data %x\n",(u32)(pcie->v_cfg1_base+ address),val);
		}
	}else	
			dw_pcie_cdm_writel(SDP_PCIE_BASE_T.cdm_vbase + (where & ~0x3),where,size,val);

	if((busdev & 0x3000000) && (address == 0x10) && (val > 0) && (val < 0xffffffff) )
	{
		dev_dbg(&pcie->pdev->dev, "(busdev & 3000000) && (address == 0x10) val =%x dusdev %x\n",val,busdev);
		bar0=val;
		pcie_prog_viewport_mem_outbound_RC(val);
	}else if ((busdev & 0x3000000) && (address == 0x18) && (val > 0) && (val < 0xffffffff) )
	{	
		dev_dbg(&pcie->pdev->dev, "(busdev & 3000000) && (address == 0x18) val =%x dusdev %x\n",val,busdev);
		bar0=val;
	}
	pcie_prog_viewport_mem_outbound_RC(bar0);
	
	spin_unlock_irqrestore(&sdp_lock, flags);

return ret;

}

static irqreturn_t sdp_pcie_isr(int irq, void *devid)
{
//	struct sdp_pcie *pcie = devid;
	u32 config9;
	u32 onepulse_config0;
	u32 val;

//	printk("irq debug\n");
	// PCIE Interrupt beacon
	val = sdp_cdm_readl(PCIe_PHY_STATUS_REG);//port logic - phy status
	val = (val | 0x1<<3); 
	sdp_cdm_writel(val,PCIe_PHY_STATUS_REG);
	

	config9 = sdp_elbi_readl(PCIE_USER_CONFIG9);
	if(config9 & (3 << 15)) {
		printk("User Config8 Error 0x%08x\n", config9);
	}
	onepulse_config0 = sdp_elbi_readl(PCIE_USER_ONEPULSE_CONFIG0);
	if(val & (0xF << 5)) {
		printk("User One Pulse Config0 Error 0x%08x\n", val);
	}

	sdp_elbi_writel(config9,PCIE_USER_CONFIG9); //clear
	sdp_elbi_writel(onepulse_config0,PCIE_USER_ONEPULSE_CONFIG0); //clear

	return IRQ_HANDLED;
}

static int preconfig_pcie_rc(struct sdp_pcie *pcie)
{
	unsigned int device_id = 0;
	unsigned int vendor_id = 0;
	u32 val;

	//Enable LTSSM (Link Training and Status State Machine)
	val = sdp_elbi_readl(PCIE_USER_CTRL5);
	sdp_elbi_writel(val | 0x8,PCIE_USER_CTRL5); //set root complex

	sdp_cdm_writel(0xfa000547,0x4);// Status & Command
#if PCIE_EP
	val = sdp_cdm_readl(0x08); //EP
	val = (val & 0x0ff) | (PCI_BASE_CLASS_BRIDGE<< 16);
	sdp_cdm_writel(val,0x08);

#else
	val = sdp_cdm_readl(0x08); //RC
	val = (val & 0x0ff) | (PCI_CLASS_BRIDGE_PCI<< 16);
//	val = (val & 0x0ff) | (PCI_BASE_CLASS_BRIDGE<< 16); not working

	sdp_cdm_writel(val,0x08);
	//RC bus setting......
	val = sdp_cdm_readl(PCI_PRIMARY_BUS); //bus set
	val &= 0xff000000;
	val |= 0x00010100;
	sdp_cdm_writel(val,PCI_PRIMARY_BUS);
#endif

	sdp_cdm_writel(0x00008108,PTR_PM_CAP+0x4);// PMCSR

	val = sdp_cdm_readl(0x00);
	device_id = (val >> 16) & 0x0000ffff;
	vendor_id = val & 0x0000ffff;
	
	dev_info(&pcie->pdev->dev, "SDP PCI Express RC driver version 1.0\n");
	dev_info(&pcie->pdev->dev, "SDP PCI Express RC VID %04xh PID %04xh\n",vendor_id,device_id);

	sdp_elbi_writel((0x3 << 15),PCIE_USER_CONFIG11); //mask int
	sdp_elbi_writel((0xf << 5),PCIE_USER_ONEPULSE_CONFIG1); //mask int

	val = sdp_cdm_readl(0x04);
	val = val | 0x7;
	sdp_cdm_writel(val,0x04);

	return 0;
}

static struct pci_ops sdp_pcie_ops = {
	.read = sdp_pcie_rd_conf,
	.write = sdp_pcie_wr_conf,
};

static int pcie_phy_init(struct device_node *np,struct sdp_pcie *pcie)
{
	int wait_flag;
	u32 val;
	unsigned long delay = jiffies + HZ*100; //4ms

	if (!np)
	return 0;
	mdelay(1000);

	if(of_property_read_u32_array(np, "samsung,pcie-phy-gpr", &sdp_pcie_phy_.phy,2)==0)
	{
		pcie->phy_base = ioremap(sdp_pcie_phy_.phy,0x1000);
		pcie->gpr_base = ioremap(sdp_pcie_phy_.gpr,0x1000);
		
	//	dev_info(&pcie->pdev->dev, "sdp pcie pll lock phy 0x%x,gpr 0x%x\n",pcie->phy_base,pcie->gpr_base);

		sdp_gpr_writel(pcie,0x00018860,0x0);
		mdelay(5);
		sdp_gpr_writel(pcie,0x00019e60,0x0); //E.->internal ,A->external
		mdelay(1);
		sdp_gpr_writel(pcie,0x00019e70,0x0);
		mdelay(1);
		sdp_gpr_writel(pcie,0x00001000,0x18);
		mdelay(1);
		val = sdp_gpr_readl(pcie,0x00);
		sdp_gpr_writel(pcie,val | 0x80000000,0x0);
		sdp_phy_writel(pcie,0x0FF,0x108);
		mdelay(1);
		sdp_phy_writel(pcie,0x0,0x54);
		mdelay(1);
		sdp_phy_writel(pcie,0x2,0xEC);
		mdelay(1);
		
		sdp_gpr_writel(pcie,0x00001000,0x18);
		mdelay(1);

		val=sdp_gpr_readl(pcie,0x0);
		mdelay(1);
		sdp_gpr_writel(pcie,val & ~0x80000000,0x0);
		mdelay(1);

		sdp_phy_writel(pcie,0x0,0x108);
		mdelay(1);		
		sdp_gpr_writel(pcie,0x00019e50,0x0);
		mdelay(1);
		sdp_gpr_writel(pcie,0x00019e70,0x0);
		mdelay(1);
		sdp_gpr_writel(pcie,0x00000002,0x18);
		mdelay(1);
		sdp_gpr_writel(pcie,0x00019eF0,0x0);
		mdelay(1);
//		sdp_gpr_writel(pcie,0x20099EF0,0x0);//gen 2 lock
//		sdp_gpr_writel(pcie,0x12,0x18);//gen 2 lock

		sdp_gpr_writel(pcie,0x33,0xc);//msb set

		// Wait Status: PLL_LOCKED == 1
		wait_flag =0;
		while(time_before(jiffies, delay))//4ms
		{
			val = sdp_gpr_readl(pcie,0x20);
			wait_flag = val & 0x1000;	//[12]: PLL_LOCK STATE
			if(wait_flag)
			{
				dev_info(&pcie->pdev->dev, "1.PLL_LOCKED success\n");
				break;
			}
		}

		iounmap(pcie->phy_base);
		iounmap(pcie->gpr_base);

	}else
	{
		sdp_pcie_phy_.phy = 0;
		sdp_pcie_phy_.gpr = 0;
	}
	
return 0;

}

static int pcie_link_up(struct sdp_pcie *pcie)
{
	u32 val;
	int wait_flag = 0;
	unsigned long delay = jiffies + HZ*10; //4ms
	
	dev_info(&pcie->pdev->dev,"pcie link up ready....\n");

	val = sdp_cdm_readl(0x78); 
	val = (val & 0xffffff1f) |(0x2<<5);//payload size 512 
	sdp_cdm_writel(val,0x78);

	sdp_elbi_writel(0xffffffff,PCIE_USER_ONEPULSE_CONFIG0);
	sdp_elbi_writel(0x20000,PCIE_USER_ONEPULSE_CONFIG1);

	sdp_elbi_writel (0xffffffff,PCIE_USER_CONFIG9);	  // clear
	sdp_elbi_writel (0x0,PCIE_USER_CONFIG11);		  // un-mask

	// clear Command register . INTx Assertion Disable bit
	val = sdp_cdm_readl  (0x04);
	val = val & 0xfffffbff; //clear:[10]
	sdp_cdm_writel(val,0x04);
	
	// clear MSI Control register . MSI Enabled bit --> Enalbe INTx instead of MSI
	val = sdp_cdm_readl  ((PTR_MSI_CAP+0x00));
	val = val & 0xfffeffff; //clear:[16]
	sdp_cdm_writel(val,(PTR_MSI_CAP+0x00));
	
	// Un-mask USER_ONEPULSE_CONFIG0 interrupt sources
	sdp_elbi_writel(0x22000,PCIE_USER_ONEPULSE_CONFIG1);
	
	// Un-mask USER_CONFIG8 interrupt sources
	sdp_elbi_writel(0x0,PCIE_USER_CONFIG11);

	// 2. waiting for XMLH_LINK_UP assertion
	wait_flag = 0;
	while(time_before(jiffies, delay))
	{
		val = sdp_elbi_readl(PCIE_USER_CONFIG0);
		wait_flag = val & 0x10; // PCIE_USER_CONFIG0[4] <= xmlh_ltssm_state[4:0] <= Current state of the LTSSM.
		if(wait_flag)
		{
			dev_info(&pcie->pdev->dev, "2.XMLH_LINK_UP assertion\n");
			break;
		}
	}
	// 3.waiting for RDLH_LINK_UP assertion
	wait_flag = 0;
	while(time_before(jiffies, delay))
	{
		val = sdp_elbi_readl(PCIE_USER_CONFIG9);
		wait_flag = val & 0x800;	  // PCIE_USER_CONFIG0[11]
		if(wait_flag)
		{
			dev_info(&pcie->pdev->dev, "3.waiting for RDLH_LINK_UP assertion\n");
			break;
		}
	}
	// 4. waiting for LTSSM == L0
	wait_flag = 0;
	
	while(time_before(jiffies, delay))
	{
	  val = sdp_elbi_readl(PCIE_USER_CONFIG1);
	  wait_flag = val & 0x3f; // PCIE_USER_CONFIG1[5:0] <= LTSSM STATE == 0x11(L0)
	  if(wait_flag == 0x11)
	  {
		  dev_info(&pcie->pdev->dev, "4.LTSSM == L0 success / LINK UP DONE !!!\n");
		  break;
	  }
	}

	return 0;	

}
struct pci_bus *sdp_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct sdp_pcie *pcie = sys_to_pcie(sys);
	
	dev_dbg(&pcie->pdev->dev,"[%s:%d]\n",__func__,__LINE__);
	
	pcie->root_bus_nr = sys->busnr;

	return pci_scan_root_bus(pcie->dev, sys->busnr, &sdp_pcie_ops, sys, &sys->resources);
}
static int sdp_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_sys_data *sys = dev->sysdata;
	struct sdp_pcie *pcie = sys->private_data;

	return pcie->irq;
}

static int sdp_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct sdp_pcie *pcie= sys_to_pcie(sys);

	dev_dbg(&pcie->pdev->dev,"[%s:%d]\n",__func__,__LINE__);
			
	if(nr == 0)
	{
		sys->mem_offset = pcie->mem.start;// 수정이 필요할듯....
		pci_add_resource(&sys->resources,&pcie->mem);
		pci_add_resource(&sys->resources, &pcie->busn);
		pci_add_resource_offset(&sys->resources, &pcie->mem,sys->mem_offset);

	}
	else{
		dev_err(&pcie->pdev->dev,"[%s:%d] wrong nr %d\n",__func__,__LINE__,nr);
		return -1;
	}

	return 1;
}

static int get_pcie_ranges(struct device_node *np,struct sdp_pcie *pcie)
{

	const int na = 3, ns = 2;
	const __be32 *range;
	int rlen, nranges, rangesz, pna,i;

	range = of_get_property(np, "ranges", &rlen);
	if (!range)
			return -EINVAL;

	pna = of_n_addr_cells(np);
	rangesz = pna + na + ns;
	nranges = rlen / sizeof(__be32) / rangesz;

	for (i = 0; i < nranges; i++) {
		u32 flags = of_read_number(range, 1);
		u32 slot = of_read_number(range + 1, 1);
		u32 cpuaddr = of_read_number(range + na, 1);
		u32 size = of_read_number(range +  na + ns, 1);
		
		if (DT_FLAGS_TO_TYPE(flags) == DT_TYPE_CON)
		{
			dev_info(&pcie->pdev->dev, "flags %x slot %d cpuaddr %x size %x\n",flags,slot,cpuaddr,size);
			pcie->p_cfg0_base = (u32)cpuaddr;
			pcie->cfg0_size = size/2;
			pcie->p_cfg1_base = (u32)pcie->p_cfg0_base + size/2;
			pcie->cfg1_size = size/2;
			dev_dbg(&pcie->pdev->dev, "p_cfg1_base %x p_cfg0_base %x size/2 %x\n",(u32)pcie->p_cfg1_base,pcie->p_cfg0_base,size/2);
			pcie->v_cfg0_base = (void *)ioremap_nocache(pcie->p_cfg0_base, size/2);
			pcie->v_cfg1_base = (void *)ioremap_nocache(pcie->p_cfg1_base, size/2);
			dev_dbg(&pcie->pdev->dev, "v_cfg1_base %x v_cfg0_base %x\n",(u32)pcie->v_cfg1_base,(u32)pcie->v_cfg0_base);

		}
		else if (DT_FLAGS_TO_TYPE(flags) == DT_TYPE_MEM32)
		{
			
			dev_info(&pcie->pdev->dev, "flags %x slot %d cpuaddr %x size %x\n",flags,slot,cpuaddr,size);
			pcie->p_mem_base = cpuaddr;
			pcie->mem_size = size;
			pcie->mem.start = cpuaddr;
			pcie->mem.end = pcie->mem.start + size-1; 
			pcie->mem.flags = IORESOURCE_MEM;
			pcie->mem.name = "PCI_MEM";

			dev_dbg(&pcie->pdev->dev,"MEM address :0x%llx\n",pcie->mem.start);
		}

		range += rangesz;
	
	}
	return 0;
}
#if !PCIE_EP
static void sdp_pcie_enable(struct sdp_pcie *pcie)
{
	struct hw_pci hw;

	memset(&hw, 0, sizeof(hw));

	hw.nr_controllers = 1;
	hw.private_data   = (void **)&pcie;
	hw.setup          = sdp_pcie_setup;
	hw.scan           = sdp_pcie_scan_bus;
	hw.map_irq        = sdp_pcie_map_irq;
	hw.ops            = &sdp_pcie_ops;

	pci_common_init(&hw);
}
#endif

static u64 sdp_dma_mask = DMA_BIT_MASK(32);

static int sdp_pcie_probe(struct platform_device *pdev)
{
	struct sdp_pcie *pcie;
	struct device_node *np = pdev->dev.of_node;
	void __iomem		*vbase ;

	int ret;
	u32 val;

#if defined(CONFIG_ARCH_SDP)
		pdev->dev.dma_mask = &sdp_dma_mask;
#if defined(CONFIG_ARM_LPAE)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else	
	if (!pdev->dev.coherent_dma_mask)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	
#endif

	pcie = devm_kzalloc(&pdev->dev, sizeof(struct sdp_pcie),
			    GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;

	SDP_PCIE_BASE_T.cdm_pbase = platform_get_resource_byname(pdev, IORESOURCE_MEM,"cdm_base");
	SDP_PCIE_BASE_T.cdm_vbase = devm_request_and_ioremap(&pdev->dev, SDP_PCIE_BASE_T.cdm_pbase);
	if (!SDP_PCIE_BASE_T.cdm_vbase ){
		dev_err(&pdev->dev,"cannot map cdm_base\n");
		ret = -EADDRNOTAVAIL;
		goto err_noclk;
	}
	dev_err(&pdev->dev,"cdm_base : 0x%x cdm_base : 0x%x\n",(u32)SDP_PCIE_BASE_T.cdm_pbase->start,(u32)SDP_PCIE_BASE_T.cdm_vbase);

	SDP_PCIE_BASE_T.elbi_pbase = platform_get_resource_byname(pdev, IORESOURCE_MEM, "elbi_base");
	SDP_PCIE_BASE_T.elbi_vbase = devm_request_and_ioremap(&pdev->dev, SDP_PCIE_BASE_T.elbi_pbase);
	if (!SDP_PCIE_BASE_T.elbi_vbase ){
		dev_err(&pdev->dev,"cannot map elbi_base\n");
		ret = -EADDRNOTAVAIL;
		goto err_noclk;
	}
	dev_err(&pdev->dev,"elbi_base : 0x%x elbi_base : 0x%x\n",(u32)SDP_PCIE_BASE_T.elbi_pbase->start,(u32)SDP_PCIE_BASE_T.elbi_vbase);
	pcie->dev = &pdev->dev;

	pcie->clk_mask= clk_get(&pdev->dev, "pcie_mask");
	if (IS_ERR(pcie->clk_mask)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", pcie->clk_mask);
	clk_prepare_enable(pcie->clk_mask);
	
	pcie->clk = clk_get(&pdev->dev, "pcie_clk");
	if (IS_ERR(pcie->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", pcie->clk);
	clk_prepare_enable(pcie->clk);

	pcie->gpr_clk = clk_get(&pdev->dev, "pcie_gpr_clk");
	if (IS_ERR(pcie->gpr_clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", pcie->gpr_clk);

	clk_prepare_enable(pcie->gpr_clk);

	pcie_phy_init(np,pcie); //set GRP phy, pll lock
#if PCIE_EP //EP setting
	dev_info(&pdev->dev,"PCIE EP MODE\n");

	val = sdp_elbi_readl(PCIE_USER_CTRL0); 
	val = (val & 0xfffffff0) | (0x0 & 0x000f); //EP mode
	sdp_elbi_writel(val,PCIE_USER_CTRL0);

	vbase=ioremap(SDP_PCIE_BASE_T.cdm_pbase->start+0x1000,0x100);
	writel(0x1fffff1,vbase+0x10); //64M
	writel(0x0,vbase+0x14);
	writel(0x1fffff1,vbase+0x18);
	writel(0,vbase+0x38);
	iounmap(vbase);
#else
	vbase=ioremap(SDP_PCIE_BASE_T.cdm_pbase->start+0x1000,0x100);
	writel(0x1fffff1,vbase+0x10); //34M
	writel(0x1fffff1,vbase+0x14);
	writel(0x1fffff1,vbase+0x18);
	writel(0x1fffff1,vbase+0x38);
	iounmap(vbase);
#endif
	val = sdp_cdm_readl(PTR_PCIE_CAP + 0x0c); //gen 2 speed
	val = (val & 0xfffffff0) | 0x2;
	sdp_cdm_writel(val,PTR_PCIE_CAP + 0x0c);

	val = sdp_cdm_readl(PTR_PL_REG + 0x10C); //speed change detect
	val = (val & 0xfffdffff) | (0x1<<17);
	sdp_cdm_writel(val,PTR_PL_REG + 0x10C);
	
//	pcie_phy_init(np,pcie); //set GRP phy, pll lock

	preconfig_pcie_rc(pcie);

	pcie_link_up(pcie);

	ret = get_pcie_ranges(np,pcie);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get ranges\n");
		ret = -ENOENT;
		goto err_clk;;
	}

	pcie->busn.name = "PCIE_BUS";
	pcie->busn.start = 0;
	pcie->busn.end = 0xff;
	pcie->busn.flags = IORESOURCE_BUS;

	pcie->root_bus_nr = -1;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		ret = -ENOENT;
		goto err_irq;
	}
	pcie->irq = ret;

	ret = request_irq(pcie->irq, sdp_pcie_isr, IRQF_SHARED, "PCIE", pcie);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IRQ: %d\n", ret);
		ret = -ENOENT;
		goto err_clk;
	}
#if !PCIE_EP
	val = sdp_elbi_readl(PCIE_USER_CONFIG1);
	val = val & 0x3f; // PCIE_USER_CONFIG1[5:0] <= LTSSM STATE == 0x11(L0)
	if(val == 0x11)	sdp_pcie_enable(pcie);
	else {
		dev_err(&pdev->dev, "Device not Detect !\n");
		goto err_irq;
	}
	pcie_prog_viewport_mem1_inbound_RC(0xc0000000);// for W projeck , 0xa... for CES
#else
	pcie_prog_viewport_mem0_inbound_EP(0,2,0x80000000,0x90000000);
	pcie_prog_viewport_mem1_outbound_EP(0x24000000);
#endif
	platform_set_drvdata(pdev, pcie);

	return 0;
		
err_irq:
	free_irq(pcie->irq, pcie);	
	
err_clk:
	clk_disable_unprepare(pcie->clk);
	clk_put(pcie->clk);	
	clk_disable_unprepare(pcie->clk_mask);
	clk_put(pcie->clk_mask);	
	
err_noclk:
 	return ret;
}

static const struct of_device_id sdp_pcie_of_match_table[] = {
	{ .compatible = "samsung,sdp-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_pcie_of_match_table);


static void sdp_pcie_shutdown(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "not working pcie shutdown\n");
}
static int sdp_pcie_remove(struct platform_device *pdev)
{
	struct sdp_pcie *pcie = platform_get_drvdata(pdev);

	free_irq(pcie->irq, pcie);	
	clk_disable_unprepare(pcie->clk);
	clk_put(pcie->clk);
	clk_disable_unprepare(pcie->clk_mask);
	clk_put(pcie->clk_mask);
/*	
	release_resource(SDP_PCIE_BASE_T.elbi_pbase);
	release_resource(SDP_PCIE_BASE_T.cdm_pbase);
	release_resource(&pcie->busn);
*/
	return 0;
}
#ifdef CONFIG_PM
static int sdp_pcie_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}
static int sdp_pcie_resume(struct platform_device *pdev)
{
	return 0;
}
#endif
static struct platform_driver sdp_pcie_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "sdp-pcie",
		.of_match_table = sdp_pcie_of_match_table,
		/* driver unloading/unbinding currently not supported */
		.suppress_bind_attrs = true,
	},
	.probe = sdp_pcie_probe,
	.remove		= sdp_pcie_remove,
	.shutdown	= sdp_pcie_shutdown,
	#ifdef CONFIG_PM
	.suspend		= sdp_pcie_suspend,
	.resume		= sdp_pcie_resume,
	#endif
};


module_platform_driver(sdp_pcie_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sdp-pcie");
