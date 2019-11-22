#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/mii.h>
#include <linux/slab.h>
#include <linux/pci_regs.h>

#include <linux/prefetch.h>
#include <asm/irq.h>

#include "hawko-pci.h"

#define DRV_NAME		"hawko-pci"
#define DRV_VERSION		"0.01"

MODULE_DESCRIPTION("samsung HawkO pci driver");
MODULE_AUTHOR("");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

u32 sdp_elbi_readl(u32 reg);
void sdp_elbi_writel(u32 val, u32 reg);
u32 sdp_cdm_readl(u32 reg);
void sdp_cdm_writel(u32 val, u32 reg);

int  pcie_prog_viewport_mem_outbound_RC(u32 bar0);//EP bar set
int  pcie_prog_viewport_mem1_outbound_RC(u32 bar0);//EP bar set
int  pcie_prog_viewport_mem1_inbound_RC(u32 phy_addr0);

static DEFINE_PCI_DEVICE_TABLE(hawko_id_table) = {
	{PCI_VENDOR_ID_HAWKO,PCI_DEVICE_ID_HAWKO,PCI_ANY_ID,PCI_ANY_ID},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hawko_id_table);
struct hawko_pci{
	void __iomem  	     *regs;
	struct pci_dev	     *pdev;
	u16	     	     chip_id;
	spinlock_t	     lock;
};
struct hawko_base{
	unsigned long bar0_start;
	unsigned long bar0_flags;
	unsigned long bar0_len;
	unsigned long bar2_start;
	unsigned long bar2_flags;
	unsigned long bar2_len;
}HAWKO_PCIE_BASE_T;

int sdp_pcie_get_outbound_atubase() // P->O access秦具窍绰 康开 林家 .
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);
	
	sdp_cdm_writel( PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX0, PCIE_ATU_VIEWPORT);
	
	return sdp_cdm_readl(PCIE_ATU_LOWER_BASE);
}
int sdp_pcie_get_outbound1_atubase() // P->O access秦具窍绰 康开 林家 .
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);
	
	sdp_cdm_writel( PCIE_ATU_REGION_OUTBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
	
	return sdp_cdm_readl(PCIE_ATU_LOWER_BASE);
}
int sdp_pcie_get_inbound_targetbase() //
{
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_AWCTRL); //MEM space..
	sdp_elbi_writel(ELBI_SLVACTL_MEM,PCIE_SLV_ARCTRL);

	sdp_cdm_writel( PCIE_ATU_REGION_INBOUND | PCIE_ATU_REGION_INDEX1, PCIE_ATU_VIEWPORT);
	
	return sdp_cdm_readl(PCIE_ATU_LOWER_TARGET);
}

int sdp_pcie_get_ep_slv()
{
	return HAWKO_PCIE_BASE_T.bar0_start;
}
int sdp_pcie_get_ep_slv_size()
{
	return HAWKO_PCIE_BASE_T.bar0_len;
}
int sdp_pcie_get_ep_mem()
{
	return HAWKO_PCIE_BASE_T.bar2_start;
}
int sdp_pcie_get_ep_mem_size()
{
	return HAWKO_PCIE_BASE_T.bar2_len;
}

EXPORT_SYMBOL(sdp_pcie_get_outbound_atubase);
EXPORT_SYMBOL(sdp_pcie_get_outbound1_atubase);
EXPORT_SYMBOL(sdp_pcie_get_inbound_targetbase);
EXPORT_SYMBOL(sdp_pcie_get_ep_slv);
EXPORT_SYMBOL(sdp_pcie_get_ep_slv_size);
EXPORT_SYMBOL(sdp_pcie_get_ep_mem);
EXPORT_SYMBOL(sdp_pcie_get_ep_mem_size);

static int hawko_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct hawko_pci * o_pci;
	int err = 0;
	u32 reg;
	
	err = pci_read_config_dword(pdev, PCI_CLASS_REVISION, &reg);
	reg = reg >> 16;
	if(reg != PCI_BASE_CLASS_BRIDGE) 
	{
		dev_err(&pdev->dev, "invaild class code %x\n",reg);
		err = -ENODEV;
		goto err_out;
	}
	
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		err = -ENODEV;
		goto err_out;
	}

	o_pci = devm_kzalloc(&pdev->dev, sizeof(struct hawko_pci), GFP_KERNEL);
	if (!o_pci) {
		dev_err(&pdev->dev, "no memory for state\n");
		err = -ENOMEM;
		goto disable_out;
	}
	pci_set_master(pdev);

	HAWKO_PCIE_BASE_T.bar0_start = pci_resource_start(pdev, 0);
	if (!HAWKO_PCIE_BASE_T.bar0_start) {
		err = -EIO;
		dev_err(&pdev->dev, "no MMIO resource\n");
		goto free_out;
	}
	HAWKO_PCIE_BASE_T.bar0_len = pci_resource_len(pdev, 0);
	if (!HAWKO_PCIE_BASE_T.bar0_len) {
		err = -EIO;
		dev_err(&pdev->dev, "no MMIO resource\n");
		goto free_out;
	}
	HAWKO_PCIE_BASE_T.bar0_flags = pci_resource_flags(pdev, 0);	
	
	dev_info(&pdev->dev,"BAR0 reg0_start = 0x%lX bytes\n", HAWKO_PCIE_BASE_T.bar0_start);
	dev_info(&pdev->dev,"BAR0 reg0_len = 0x%lX bytes\n", HAWKO_PCIE_BASE_T.bar0_len);

	HAWKO_PCIE_BASE_T.bar2_start = pci_resource_start(pdev, 2);
	if (!HAWKO_PCIE_BASE_T.bar0_start) {
		err = -EIO;
		dev_err(&pdev->dev, "no MMIO resource\n");
		goto free_out;
	}
	HAWKO_PCIE_BASE_T.bar2_len = pci_resource_len(pdev, 2);
	if (!HAWKO_PCIE_BASE_T.bar0_len) {
		err = -EIO;
		dev_err(&pdev->dev, "no MMIO resource\n");
		goto free_out;
	}
	HAWKO_PCIE_BASE_T.bar2_flags = pci_resource_flags(pdev, 2);	
	/* aitu outbound */
	pcie_prog_viewport_mem_outbound_RC(HAWKO_PCIE_BASE_T.bar0_start);
	pcie_prog_viewport_mem1_outbound_RC(HAWKO_PCIE_BASE_T.bar2_start);
	
	dev_info(&pdev->dev,"BAR2 reg2_start = 0x%lX bytes\n", HAWKO_PCIE_BASE_T.bar2_start);
	dev_info(&pdev->dev,"BAR2 reg2_len = 0x%lX bytes\n", HAWKO_PCIE_BASE_T.bar2_len);
	
	/* Check for weird/broken PCI region reporting */
	if (HAWKO_PCIE_BASE_T.bar0_len < BAR0_SIZE) {
		dev_err(&pdev->dev,"Invalid PCI region sizes\n");
		err = -ENODEV;
		goto free_out;
	}
	err = pci_request_regions(pdev, DRV_NAME);
	if (err) 
	{
		dev_err(&pdev->dev, "pci_request_regions fail\n");
		err = -ENODEV;
		goto free_out;
	}		
	o_pci->pdev = pdev;
	
	spin_lock_init(&o_pci->lock);

	dev_info(&pdev->dev,"HawkO PCIE control Driver\n");

	return err;

	free_out:
		devm_kfree(&pdev->dev,o_pci);
	disable_out:
		pci_disable_device(pdev);
	err_out:
		return err;	

}
static void hawko_remove(struct pci_dev *pdev)
{
	printk("[%s:%d]\n",__func__,__LINE__);
	pci_disable_device(pdev);
}
static void hawko_shutdown(struct pci_dev *pdev)
{
	printk("[%s:%d]\n",__func__,__LINE__);
}

static struct pci_driver hawko_driver = {
	.name =         DRV_NAME,
	.id_table =     hawko_id_table,
	.probe =        hawko_probe,
	.remove =       hawko_remove,
	.shutdown =	hawko_shutdown,
};

static int __init hawko_init_module(void)
{
	/* TODO */
	//	....
	printk("[%s:%d]\n",__func__,__LINE__);
	return pci_register_driver(&hawko_driver);
}

static void __exit hawko_cleanup_module(void)
{	printk("[%s:%d]\n",__func__,__LINE__);
	pci_unregister_driver(&hawko_driver);
}


module_init(hawko_init_module);
module_exit(hawko_cleanup_module);


