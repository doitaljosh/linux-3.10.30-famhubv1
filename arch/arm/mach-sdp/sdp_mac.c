/***************************************************************************
 *
 *	arch/arm/plat-sdp/sdp_mac.c
 *	Samsung Elecotronics.Co
 *	Created by tukho.kim
 *
 * ************************************************************************/
/*
 * 2009.08.02,tukho.kim: Created by tukho.kim@samsung.com
 * 2009.08.31,tukho.kim: Revision 1.00
 * 2009.09.23,tukho.kim: re-define drv version 0.90
 * 2009.09.27,tukho.kim: debug dma_free_coherent and modify probe, initDesc 0.91
 * 2009.09.27,tukho.kim: add power management function drv version 0.92
 * 2009.09.28,tukho.kim: add retry count, when read mac address by i2c bus  0.921
 * 2009.10.06,tukho.kim: debug when rx sk buffer allocation is failed 0.93
 * 2009.10.19,tukho.kim: rx buffer size is fixed, ETH_DATA_LEN 1500 0.931
 * 2009.10.22,tukho.kim: debug when rx buffer alloc is failed 0.940
 * 2009.10.25,tukho.kim: recevice packet is error, not re-alloc buffer 0.941
 * 2009.10.27,tukho.kim: mac configuration initial value is "set watchdog disable 0.942
 * 2009.11.05,tukho.kim: debug to check tx descriptor in hardStart_xmit 0.943
 * 2009.11.18,tukho.kim: rtl8201E RXD clock toggle active, rtl8201E hidden register 0.9431
 * 2009.11.21,tukho.kim: modify netDev_rx routine 0.95
 * 2009.12.01,tukho.kim: debug phy check media, 0.951
 * 			 full <-> half ok, 10 -> 100 ok, 100 -> 10 need to unplug and plug cable
 * 2009.12.02,tukho.kim: debug module init, exit  0.952
 * 2009.12.15,tukho.kim: hwSpeed init value change to STATUS_SPEED_100 0.953
 * 2009.12.15,tukho.kim: when alloc rx buffer,  align rx->data pointer by 16 0.954
 * 2009.12.28,tukho.kim: remove to check magic code when read mac address by i2c 0.9541
 * 2010.01.13,tukho.kim: debug sdpGmac_rxDescRecovery function 0.955
 * 2010.01.23,tukho.kim: debug wait code about DMA reset in sdpGmac_reset  0.956
 * 2010.06.25,tukho.kim: rename file sdpGmacInit.c to sdp_mac.c and merge with sdpGmacBase.c  0.957
 * 2010.09.27,tukho.kim: bug fix, cortex shared device region(write buffer) 0.958
 * 2010.09.29,tukho.kim: bug fix, when insmod, No such device 0.959
 * 2010.09.29,tukho.kim: bug fix, remove reverse clock configuration 0.9591
 * 2011.04.29,tukho.kim: replace dsb with wmb (outer cache) 0.9592
 * 2011.11.18,tukho.kim: add function sdpGmac_txQptr_replacement and it run when meet to tx error 0.96
 * 2011.11.30,tukho.kim: buf fix, sdpGmac_txQptr_replacement and add mac dma hw info 0.962
 * 2011.12.07,tukho.kim: buf fix, sdpGmac_setTxQptr ring buffer 0.963
 * 2011.12.07,tukho.kim: buf fix, phy initialize by lpa register 0.964
 * 2011.12.14,tukho.kim: buf fix, control variable and hw memory 0.965
 * 2012.04.20,drain.lee: delete phy code, and use phy_device subsystem. change MAC addr setting sequence.  0.966
 * 2012.04.23,drain.lee: change to use NAPI subsystem.  0.967
 * 2012.05.11,drain.lee: interrupt error fix(use masked status). change reset timeout 5sec 0.968
 * 2012.05.14,drain.lee: move chip dependent code. 0.969
 * 2012.05.14,drain.lee: patch, linux-2.6.35.11 r322:323 0.970(modify txqptr control value and descriptor)
 * 2012.05.14,drain.lee: patch, linux-2.6.35.11 r335:336 0.971(2012/01/03 release version)
 * 2012.05.25,drain.lee: bug fix, rxDescRecovery error fix.
 * 2012.05.27,drain.lee: bug fix, scheduling while atomic in sdpGmac_netDev_ioctl().
 * 2012.07.23,drain.lee: bug fix, phy_device control flow.
 * 2012.07.24,drain.lee: bug fix, Phy device irq bug fix.
 * 2012.08.03,drain.lee: bug fix, Multicast mac address setting sequence. v0.975
 * 2012.08.03,drain.lee: change phy attach at open. v0.976
 * 2012.08.29,drain.lee: support Normal desc v0.977
 * 2012.08.29,drain.lee: buf fix, rx buf size error in Normal desc v0.978
 * 2012.10.05,drain.lee: add selftest(loopback test) for ethtool v0.979
 * 2012.10.18,drain.lee: change selftest(loopback test) use ETH_TEST_FL_EXTERNAL_LB flag v0.980
 * 2012.12.13,drain.lee: add debug dump in selftest. v0.981
 * 2012.12.14,drain.lee: bug fix, size of cache operation incorrect. v0.982
 * 2012.12.14,drain.lee: change debug dump v0.983
 * 2012.12.17,drain.lee: move dma mask setting to sdpxxxx.c file v0.984
 * 2012.12.21,drain.lee: fix compile warning v0.985
 * 2012.12.28,drain.lee: bug fix, tx unmap DMA_FROM_DEVICE -> DMA_TO_DEVICE v0.986
 * 2013.01.15,drain.lee: fix, selftest(ext lb error). v0.987
 * 2013.01.15,drain.lee: add reg dump in abnormal interrupt. v0.988
 * 2013.01.24,drain.lee: bugfix, rx desc set zero... v0.989b
 * 2013.01.31,drain.lee: bugfix, Rx/Tx Qptr critical section...refactoring rx/tx ring v0.990b
 * 2013.01.31,drain.lee: fix, fix adjust link. only if status is change, be applied config v0.991b
 * 2013.03.05,drain.lee: porting to linux3.8(fix compile error) v0.992
 * 2013.03.05,drain.lee: fix, prevent defect(Buffer not null terminated) v0.993
 * 2013.03.13,drain.lee: change descripter alignment v0.995
 * 2013.03.13,drain.lee: change print error msg, ring functions v0.996
 * 2013.03.15,drain.lee: support ehttool stats v0.997
 * 2013.05.03,drain.lee: fix MII Mode print v0.998
 * 2013.05.07,drain.lee: remove dummy skb v0.999
 * 2013.05.07,drain.lee: support suspend/resume, remove mac from i2c v0.999.1
 * 2013.05.07,drain.lee: fix skb align use bus whdth v0.999.2
 * 2013.05.28,drain.lee: fix suspend to RAM error. v0.999.3
 * 2013.07.26,drain.lee: support DT, and bus align fix v0.999.4
 * 2013.08.05,drain.lee: fix reset waiting time. v0.999.5
 * 2013.08.06,drain.lee: change selftest seq. v0.999.6
 * 2013.08.07,drain.lee: bugfix, if rx frame is error, desc end of ring maker is wrong position v1.00
 * 2013.08.08,drain.lee: change, even if netdev do not running, allow access mdio ioctl.  v1.01
 * 2013.08.08,drain.lee: bugfix, selftest memory leak.   v1.02
 * 2013.09.12,drain.lee: remove PHY genphy_suspend/resume code   v1.03
 * 2013.09.30,drain.lee: change selftest timeout 100ms   v1.04
 * 2013.10.22,drain.lee: support multi MAC device  v1.05
 * 2013.10.25,drain.lee: support fixed link v1.06
 * 2013.11.01,drain.lee: bugfix, NULL pointer deref v1.07
 * 2013.11.08,drain.lee: change phy connect sequence v1.08
 * 2014.08.25,drain.lee: add calculated CSR clock rate. v1.09
 * 2014.08.25,drain.lee: bugfix, incorrect multicast address setting. v1.10
 * 2014.09.26,drain.lee: apply suspend early(linkup fix) v1.11
 * 2014.09.26,drain.lee: fix compile warning(W=123). v1.12
 * 2014.09.26,drain.lee: fix phy find, fix selftest v1.15
 * 2014.11.27,drain.lee: bugfix selftest v1.16
 * 2014.11.28,drain.lee: add debugfs v1.17
 * 2014.12.22,drain.lee: fix start polling timer at linkup v1.18
 * 2014.12.24,drain.lee: fix polling internal to 4ms v1.19
 * 2015.01.07,drain.lee: support RTL8304E switch v1.20
 * 2015.01.07,drain.lee: add hawk-a rev0 fixup v1.21
 * 2015.01.13,drain.lee: supprot RTL8304E switch(broadcast disable) v1.22
 * 2015.01.19,drain.lee: add valid check for MAC address setting v1.23
 * 2015.02.11,drain.lee: add mdio timeout and debug log v1.24
 */

#ifdef CONFIG_SDP_MAC_DEBUG
#define DEBUG
#endif

#include <linux/time.h>		// for debugging
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/circ_buf.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <mach/soc.h>
#include "sdp_mac.h"

#define GMAC_DRV_VERSION	"v1.24(add mdio timeout and debug log)"


struct sdp_mac_mdiobus_priv {
	SDP_GMAC_DEV_T *pGmacDev;
	u32 addr;
	u32 data;
	u32 cr;
};

static inline u32 sdp_mac_readl(const volatile void __iomem *addr)
{
	return readl(addr);
}

static inline void sdp_mac_writel(const u32 val, volatile void __iomem *addr)
{
	writel(val, addr);
}

static void sdp_mac_gmac_dump(SDP_GMAC_DEV_T *pGmacDev)
{
	SDP_GMAC_T *pGmacBase = pGmacDev->pGmacBase;
	u32 gmac_reg_dump[0xC0/sizeof(u32)];
	resource_size_t phy_base =
		platform_get_resource(to_platform_device(pGmacDev->pDev), IORESOURCE_MEM, 0)->start;
	u32 i;

	for(i = 0; i < ARRAY_SIZE(gmac_reg_dump); i++) {
		gmac_reg_dump[i] = sdp_mac_readl(((u32 *)pGmacBase) + i);
	}

	dev_info(pGmacDev->pDev, "===== SDP-MAC GMAC DUMP (VA: 0x%p, PA: 0x%08llx) =====\n",
		pGmacBase, (u64)phy_base);
	for(i = 0; i < ARRAY_SIZE(gmac_reg_dump); i+=4) {
		dev_info(pGmacDev->pDev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				(u64)phy_base+(i*sizeof(gmac_reg_dump[0])),
				gmac_reg_dump[i+0],
				gmac_reg_dump[i+1],
				gmac_reg_dump[i+2],
				gmac_reg_dump[i+3]
			  );
	}
	pr_info("\n");
}

static void sdp_mac_dma_dump(SDP_GMAC_DEV_T *pGmacDev)
{
	SDP_GMAC_DMA_T *pDmaBase = pGmacDev->pDmaBase;
	u32 dma_reg_dump[0x60/sizeof(u32)];
	resource_size_t phy_base =
		platform_get_resource(to_platform_device(pGmacDev->pDev), IORESOURCE_MEM, 4)->start;
	u32 i;

	for(i = 0; i < ARRAY_SIZE(dma_reg_dump); i++) {
		if(i == 0x20/sizeof(u32)) {//skip overflow counter
			dma_reg_dump[i] = 0xdeadc0de;
		} else {
			dma_reg_dump[i] = sdp_mac_readl(((u32 *)pDmaBase) + i);
		}
	}

	dev_info(pGmacDev->pDev, "===== SDP-MAC DMA DUMP (VA: 0x%p, PA: 0x%08llx) =====\n",
		pDmaBase, (u64)phy_base);
	for(i = 0; i < ARRAY_SIZE(dma_reg_dump); i+=4) {
		dev_info(pGmacDev->pDev, "0x%08llx: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				(u64)phy_base+(i*sizeof(dma_reg_dump[0])),
				dma_reg_dump[i+0],
				dma_reg_dump[i+1],
				dma_reg_dump[i+2],
				dma_reg_dump[i+3]
			  );
	}
	pr_info("\n");
}

static void
sdp_mac_phy_regdump(SDP_GMAC_DEV_T *pGmacDev, const char *header)
{
	struct phy_device *phydev = pGmacDev->phydev;
	u32 i;

	if(!phydev) {
		dev_info(pGmacDev->pDev, "sdp_mac_phy_regdump(): Phy is NULL!\n");
		return;
	}

	if(header == NULL) {
		header = "Phy Dump";
	}

	dev_info(pGmacDev->pDev, "%s(phy addr: %d)\n", header, phydev->addr);
	for(i = 0; i < 0x20; i+=4) {
		dev_info(pGmacDev->pDev, "Phy Dump %2d: 0x%04x 0x%04x 0x%04x 0x%04x\n",
			i, phy_read(phydev, i+0), phy_read(phydev, i+1),
			phy_read(phydev, i+2), phy_read(phydev, i+3));
	}

}

static int sdp_mac_mdio_wait(struct mii_bus *bus)
{
	struct sdp_mac_mdiobus_priv *priv = bus->priv;
	SDP_GMAC_DEV_T *pGmacDev = priv->pGmacDev;
	const unsigned int udelay_time = 10;
	int busy_count = 0;
	u32 mdio_ctrl = 0;

	while(true) {
		mdio_ctrl = sdp_mac_readl((void *)priv->addr);
		if(!(mdio_ctrl & B_GMII_ADDR_BUSY)) {
			break;
		}

		if(busy_count >= 100000) {
			dev_err(pGmacDev->pDev, "MDIO busy wait timeout!!"
				"(busy_count %d * %dus,  mdio_ctrl 0x%08x, priv->addr 0x%08x, priv->data 0x%08x)\n",
				busy_count, udelay_time, mdio_ctrl, priv->addr, priv->data);
			sdp_mac_gmac_dump(pGmacDev);
			sdp_mac_dma_dump(pGmacDev);
			return -EBUSY;
		}

		udelay(udelay_time);
		busy_count++;
	}

	return busy_count;
}

static int sdp_mac_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct sdp_mac_mdiobus_priv *priv = bus->priv;
	SDP_GMAC_DEV_T *pGmacDev = priv->pGmacDev;
	int ret = 0;

	ret = sdp_mac_mdio_wait(bus);
	if(ret < 0) {
		dev_err(pGmacDev->pDev, "MDIO read already busy!!"
			"(ret %d, phyid %d, regnum %d)\n",
			ret, phy_id, regnum);
		return -0x10000;/*0xFFFF0000*/
	}

	sdp_mac_writel(GMII_ADDR_READ(phy_id, regnum)|(priv->cr<<2), (void *)priv->addr);

	ret = sdp_mac_mdio_wait(bus);
	if(ret < 0) {
		dev_err(pGmacDev->pDev, "MDIO read failed!!"
			"(ret %d, phyid %d, regnum %d)\n",
			ret, phy_id, regnum);
		return -0x10000;/*0xFFFF0000*/
	}

	return sdp_mac_readl((void *)priv->data)&0xFFFF;
}

static int sdp_mac_mdio_write(struct mii_bus *bus, int phy_id, int regnum, u16 val)
{
	struct sdp_mac_mdiobus_priv *priv = bus->priv;
	SDP_GMAC_DEV_T *pGmacDev = priv->pGmacDev;
	int ret = 0;

	ret = sdp_mac_mdio_wait(bus);
	if(ret < 0) {
		dev_err(pGmacDev->pDev, "MDIO write already busy!!"
			"(ret %d, phyid %d, regnum %d, val %u)\n",
			ret, phy_id, regnum, val);
		return ret;
	}


	sdp_mac_writel(val, (void *)priv->data);
	sdp_mac_writel(GMII_ADDR_WRITE(phy_id, regnum)|(priv->cr<<2), (void *)priv->addr);

	ret = sdp_mac_mdio_wait(bus);
	if(ret < 0) {
		dev_err(pGmacDev->pDev, "MDIO write failed!!"
			"(ret %d, phyid %d, regnum %d, val %u)\n",
			ret, phy_id, regnum, val);
		return ret;
	}

	return 0;
}

static phy_interface_t sdp_mac_get_interface(struct net_device *pNetDev)
{
	SDP_GMAC_DEV_T	*pGmacDev = netdev_priv(pNetDev);
	u32 hw_feature;

	hw_feature = pGmacDev->pDmaBase->hwfeature;

	switch(B_ACTPHYIF(hw_feature))
	{
	case 0:
		return PHY_INTERFACE_MODE_MII;
	case 1:
		return PHY_INTERFACE_MODE_RGMII;
	case 2:
		return PHY_INTERFACE_MODE_SGMII;
	case 3:
		return PHY_INTERFACE_MODE_TBI;
	case 4:
		return PHY_INTERFACE_MODE_RMII;
	case 5:
		return PHY_INTERFACE_MODE_RTBI;
	case 6:
	case 7:
	default:
		dev_err(pGmacDev->pDev, "Not suppoted phy interface!!!(%#x)\n", B_ACTPHYIF(hw_feature));
		return PHY_INTERFACE_MODE_NA;
	}
}

static void
sdp_ring_clear(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring)
{
	unsigned long head, tail;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);

	head = ACCESS_ONCE(ring->head);
	tail = ring->tail;

	while(CIRC_CNT(head, tail, ring->size) >= 1) {
		DMA_DESC_T* desc;
		u32 length = 0;

		/* read index before reading contents at that index */
		smp_read_barrier_depends();

		desc = &ring->pdesc[tail];

		length = DESC_GET_SIZE1(desc->length);
		if((length) && (desc->data1)){
			DPRINTK_GMAC("%s ring %lu clear length1 %x, data1 %x\n",
				ring->name, tail, length, desc->data1);
			dma_unmap_single(pGmacDev->pDev, desc->buffer1,
					length, DMA_FROM_DEVICE);
			dev_kfree_skb((struct sk_buff*) desc->data1);
		}

		length = DESC_GET_SIZE2(desc->length);
		if((length) && (desc->data2)){
			DPRINTK_GMAC("%s ring %lu clear length2 %x, data1 %x\n",
				ring->name, tail, length, desc->data2);
			dma_unmap_single(pGmacDev->pDev, desc->buffer2,
					length, DMA_FROM_DEVICE);
			dev_kfree_skb((struct sk_buff*) desc->data2);
		}

		memset(desc, 0, sizeof(*desc));

		smp_mb(); /* finish reading descriptor before incrementing tail */

		ring->tail = (tail + 1) & (ring->size - 1);

		/* re read head, tail */
		head = ACCESS_ONCE(ring->head);
		tail = ring->tail;
	}

	spin_unlock_irqrestore(&ring->lock, flags);
}

static int
sdp_ring_set_index(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring, unsigned long index)
{
	int ret = 0;
	unsigned long count = 0;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);

	if(index >= ring->size) {
		dev_err(pGmacDev->pDev, "error! %s ring size %lu >= index %lu\n", ring->name, ring->size, index);
		ret = -EINVAL;
		goto unlock;
	}

	count = CIRC_CNT(ring->head, ring->tail, ring->size);

	if((count > 0) || (ring->head != ring->tail)){
		dev_err(pGmacDev->pDev, "error! %s ring is not empty! count %lu\n", ring->name, count);
		ret = -EPERM;
		goto unlock;
	}

	/* set index */
	ring->head = ring->tail = index;
	ret = (int)index;

unlock:
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}

inline static unsigned long
sdp_ring_count(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring)
{
	unsigned long count = 0;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	count = CIRC_CNT(ring->head, ring->tail, ring->size);
	spin_unlock_irqrestore(&ring->lock, flags);

	return count;
}

inline static unsigned long
sdp_ring_space(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring)
{
	unsigned long space = 0;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	space = CIRC_SPACE(ring->head, ring->tail, ring->size);
	spin_unlock_irqrestore(&ring->lock, flags);

	return space;
}

inline static bool
sdp_ring_is_empty(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring)
{
	return sdp_ring_count(pGmacDev, ring) == 0;
}

inline static bool
sdp_ring_is_full(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring)
{
	return sdp_ring_space(pGmacDev, ring) == 0;
}

static int
sdp_ring_push(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring, DMA_DESC_T* set_desc)
{
	int ret = 0;
	unsigned long head, tail;
	bool in_softirq = in_softirq();

	WARN(in_irq(), "%s: called in IRQ!!", __FUNCTION__);
	if(in_softirq)
		spin_lock(&ring->lock);
	else
		spin_lock_bh(&ring->lock);

	head = ring->head;
	tail = ACCESS_ONCE(ring->tail);

	if(CIRC_SPACE(head, tail, ring->size) >= 1) {
		sdp_mac_desc_info_t info;
		/* insert one item into the buffer */
		if(!ring->push) {
			ret = -EFAULT;
			goto unlock;
		}

		info.index = head;
		info.desc = &ring->pdesc[head];
		info.ring_size = ring->size;

		if(!ring->push(pGmacDev, &info, set_desc)) {
			ret = -EPERM;
			goto unlock;
		}

		smp_wmb(); /* commit the item before incrementing the head */

		ring->head = (head + 1) & (ring->size - 1);
		ret = (int)head;
	} else {
		dev_printk(KERN_ERR, pGmacDev->pDev, "%s ring is full! head %lu, tail %lu\n", ring->name, head, tail);
		ret = -ENOSPC;
	}

unlock:
	if(in_softirq)
		spin_unlock(&ring->lock);
	else
		spin_unlock_bh(&ring->lock);
	return ret;
}

static int
sdp_ring_pull(SDP_GMAC_DEV_T* pGmacDev, struct sdp_mac_desc_ring *ring, DMA_DESC_T* get_desc)
{
	int ret = 0;
	unsigned long head, tail;
	bool in_softirq = in_softirq();

	WARN(in_irq(), "%s: called in IRQ!!", __FUNCTION__);
	if(in_softirq)
		spin_lock(&ring->lock);
	else
		spin_lock_bh(&ring->lock);

	head = ACCESS_ONCE(ring->head);
	tail = ring->tail;

	if (CIRC_CNT(head, tail, ring->size) >= 1) {
		sdp_mac_desc_info_t info;

		/* read index before reading contents at that index */
		smp_read_barrier_depends();

		/* extract one item from the buffer */
		if(!ring->pull) {
			ret = -EFAULT;
			goto unlock;
		}

		info.index = tail;
		info.desc = &ring->pdesc[tail];
		info.ring_size = ring->size;

		if(!ring->pull(pGmacDev, &info, get_desc)) {
			ret = -EPERM;
			goto unlock;
		}

		smp_mb(); /* finish reading descriptor before incrementing tail */

		ring->tail = (tail + 1) & (ring->size - 1);
		ret = (int)tail;
	} else {
		dev_printk(KERN_ERR, pGmacDev->pDev, "%s ring is empty! head %lu, tail %lu\n", ring->name, head, tail);
		ret = -ENOSPC;
	}

unlock:
	if(in_softirq)
		spin_unlock(&ring->lock);
	else
		spin_unlock_bh(&ring->lock);

	return ret;
}

static bool
sdp_mac_push_rxdesc(SDP_GMAC_DEV_T *pGmacDev, sdp_mac_desc_info_t *info, DMA_DESC_T *set_desc/*IN*/) {

	if(info->desc->status & RDES0_OWN) {
		DPRINTK_GMAC_ERROR("rx desc%lu status is desc own by dma\n", info->index);
		return false;
	}

	if(!sdpGmac_descEmpty(info->desc)) {
		DPRINTK_GMAC_ERROR("rx desc%lu is Not Empty!\n", info->index);
		return false;
	}

	if(sdpGmac_rxDescChained(set_desc)){
		DPRINTK_GMAC_ERROR("rx chained desc Not Implenedted yet\n");
		return false;
	} else {
		/* copy desc without OWN bit */
		info->desc->status = set_desc->status & ~RDES0_OWN;
		info->desc->length = set_desc->length;
		info->desc->buffer1 = set_desc->buffer1;
		info->desc->buffer2 = set_desc->buffer2;
		info->desc->data1 = set_desc->data1;
		info->desc->data2 = set_desc->data2;

		if(info->index == (info->ring_size-1)) {
			RDES_RER_SET((*info->desc), 1);
		}
		wmb();
		info->desc->status |= RDES0_OWN;
		if(!(info->desc->status & RDES0_OWN)) wmb();
	}
	return true;
}

static bool
sdp_mac_pull_rxdesc(SDP_GMAC_DEV_T *pGmacDev, sdp_mac_desc_info_t *info, DMA_DESC_T *get_desc/*OUT*/) {

	if(info->desc->status & RDES0_OWN) {
		DPRINTK_GMAC_FLOW("rx desc%lu status is desc own by dma\n", info->index);
		return false;
	}

	if(sdpGmac_descEmpty(info->desc)) {
		DPRINTK_GMAC_ERROR("rx desc%lu status empty\n", info->index);
		return false;
	}

	if(sdpGmac_rxDescChained(info->desc)){
		DPRINTK_GMAC_ERROR("rx chained desc Not Implenedted yet\n");
		return false;
	} else {
		memcpy(get_desc, info->desc, sizeof(DMA_DESC_T));
		memset(info->desc, 0, sizeof(DMA_DESC_T));
	}
	return true;
}

static bool
sdp_mac_push_txdesc(SDP_GMAC_DEV_T *pGmacDev, sdp_mac_desc_info_t *info, DMA_DESC_T *set_desc/*IN*/) {

	if(info->desc->status & RDES0_OWN) {
		DPRINTK_GMAC_ERROR("tx desc%lu status is desc own by dma\n", info->index);
		return false;
	}

	if(!sdpGmac_descEmpty(info->desc)) {
		DPRINTK_GMAC_ERROR("tx desc%lu is Not Empty!\n", info->index);
		return false;
	}

	if(sdpGmac_rxDescChained(set_desc)){
		DPRINTK_GMAC_ERROR("tx chained desc Not Implenedted yet\n");
		return false;
	} else {
		if(info->index == (info->ring_size-1)) {
			TDES_TER_SET((*set_desc), 1);/* Set Rx End of ring */
		}

		info->desc->status = set_desc->status & ~TDES0_OWN;
		info->desc->length = set_desc->length;
		info->desc->buffer1 = set_desc->buffer1;
		info->desc->data1 = set_desc->data1;
		info->desc->buffer2 = set_desc->buffer2;
		info->desc->data2 = set_desc->data2;
		wmb();
		info->desc->status |= TDES0_OWN;
		if(!(info->desc->status & TDES0_OWN)) wmb();
	}
	return true;
}

static bool
sdp_mac_pull_txdesc(SDP_GMAC_DEV_T *pGmacDev, sdp_mac_desc_info_t *info, DMA_DESC_T *get_desc/*OUT*/) {

	if(info->desc->status & TDES0_OWN) {
		DPRINTK_GMAC_FLOW("tx desc%lu status is desc own by dma\n", info->index);
		return false;
	}

	if(sdpGmac_descEmpty(info->desc)) {
		DPRINTK_GMAC_ERROR("tx desc%lu status empty\n", info->index);
		return false;
	}

	if(sdpGmac_rxDescChained(info->desc)){
		DPRINTK_GMAC_ERROR("tx chained desc Not Implenedted yet\n");
		return false;
	} else {
		memcpy(get_desc, info->desc, sizeof(DMA_DESC_T));
		memset(info->desc, 0, sizeof(DMA_DESC_T));
	}
	return true;
}

static void
sdp_mac_ring_init(SDP_GMAC_DEV_T* pGmacDev)
{
	pGmacDev->rx_ring.name = "rx desc";
	spin_lock_init(&pGmacDev->rx_ring.lock);
	pGmacDev->rx_ring.push = sdp_mac_push_rxdesc;
	pGmacDev->rx_ring.pull = sdp_mac_pull_rxdesc;
	pGmacDev->rx_ring.size = 0x1UL << (fls(NUM_RX_DESC)-1);
	pGmacDev->rx_ring.head = 0;
	pGmacDev->rx_ring.tail = 0;
	pGmacDev->rx_ring.pdesc = pGmacDev->pRxDesc;


	pGmacDev->tx_ring.name = "tx desc";
	spin_lock_init(&pGmacDev->tx_ring.lock);
	pGmacDev->tx_ring.push = sdp_mac_push_txdesc;
	pGmacDev->tx_ring.pull = sdp_mac_pull_txdesc;
	pGmacDev->tx_ring.size = 0x1UL << (fls(NUM_TX_DESC)-1);
	pGmacDev->tx_ring.head = 0;
	pGmacDev->tx_ring.tail = 0;
	pGmacDev->tx_ring.pdesc = pGmacDev->pTxDesc;
}

/* do align skb start, return reserved size */
static inline u32
sdpGmac_skbAlign(SDP_GMAC_DEV_T* pGmacDev, struct sk_buff *pSkb) {
	u32 offset = (u32)pSkb->data & (~pGmacDev->bus_mask);
	u32 reserve_size = 0;

	/* buffer start BUS Width align */
	if(offset) {
		reserve_size = pGmacDev->bus_align - offset;
	}

	/* IP align */
	skb_reserve(pSkb, (int)reserve_size + NET_IP_ALIGN);

	return reserve_size + NET_IP_ALIGN;
}

static void
sdpGmac_netDev_tx(struct net_device *pNetDev, int budget)
{
	SDP_GMAC_DEV_T	*pGmacDev = netdev_priv(pNetDev);

	struct net_device_stats *pNetStats = &pGmacDev->netStats;
	DMA_DESC_T	txDesc;

	u32 checkStatus;
	int descIndex;
	int cleaned = 0;

	DPRINTK_GMAC_FLOW("called\n");

	while(!sdp_ring_is_empty(pGmacDev, &pGmacDev->tx_ring)) {
		if(budget != 0 && cleaned >= budget) break;

		descIndex = sdp_ring_pull(pGmacDev, &pGmacDev->tx_ring, &txDesc);

		cleaned++;

		if(descIndex < 0) break;	// break condition
		if(!txDesc.data1) continue;	// not receive

		DPRINTK_GMAC_FLOW("Tx Desc %d for skb 0x%08x whose status is %08x\n",
					descIndex, txDesc.data1, txDesc.status);

		dma_unmap_single(pGmacDev->pDev, txDesc.buffer1,
				DESC_GET_SIZE1(txDesc.length), DMA_TO_DEVICE);

		dev_kfree_skb_any((struct sk_buff*)txDesc.data1);

		// ENH_MODE
		checkStatus = (txDesc.status & 0xFFFF) & TDES0_ES;

		if(!checkStatus) {
			pNetStats->tx_bytes += txDesc.length & DESC_SIZE_MASK;
			pNetStats->tx_packets++;
		}
		else {	// ERROR
			pNetStats->tx_errors++;

			if(txDesc.status & (TDES0_LCO | TDES0_EC))
				pNetStats->tx_aborted_errors++ ;
			if(txDesc.status & (TDES0_LC | TDES0_NC))
				pNetStats->tx_carrier_errors++;
		}

		pNetStats->collisions += TDES0_CC_GET(txDesc.status);

	}

	netif_wake_queue(pNetDev);

	DPRINTK_GMAC_FLOW("exit\n");
	return;
}


/* budget : maximum number of descs that processed in rx call.
			0 is inf
*/
static int
sdpGmac_netDev_rx(struct net_device *pNetDev, int budget)
{
	SDP_GMAC_DEV_T	*pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	struct net_device_stats *pNetStats = &pGmacDev->netStats;
	struct sk_buff * pSkb;
	DMA_DESC_T	rxDesc;

	u32 checkStatus;
	u32 len;
	int descIndex;
	dma_addr_t rxDmaAddr;
	int nrChkDesc = 0;	// packet


	DPRINTK_GMAC_FLOW("called\n");

	while(!sdp_ring_is_empty(pGmacDev, &pGmacDev->rx_ring)){
		/* check budget */
		if((budget != 0) && (nrChkDesc >= budget)) {
			break;
		}

		descIndex = sdp_ring_pull(pGmacDev, &pGmacDev->rx_ring, &rxDesc);

		if(descIndex < 0) {
			break;	// break condition
		}

		nrChkDesc++;

		DPRINTK_GMAC("Rx Desc %d for skb 0x%08x whose status is %08x\n",
				descIndex, rxDesc.data1, rxDesc.status);


		dma_unmap_single(pGmacDev->pDev, rxDesc.buffer1, RDES0_FL_GET(rxDesc.status), DMA_FROM_DEVICE);

		pSkb = (struct sk_buff*)rxDesc.data1;

if(false) {
	dev_printk(KERN_DEBUG, pGmacDev->pDev, "Rx flags(%08x:%s%s%s%s%s%s) Rx byte:%lu\n",
		rxDesc.status,
		rxDesc.status&RDES0_LE ? "LE ":"",
		rxDesc.status&RDES0_OE ? "OE ":"",
		rxDesc.status&RDES0_LC ? "LC ":"",
		rxDesc.status&RDES0_RWT ? "RWT ":"",
		rxDesc.status&RDES0_DBE ? "DBE ":"",
		rxDesc.status&RDES0_CE ? "CE ":"",
		RDES0_FL_GET(rxDesc.status));

	print_hex_dump(KERN_DEBUG, "Rx buffer: ", DUMP_PREFIX_ADDRESS, 16, 1,
	     pSkb->data, RDES0_FL_GET(rxDesc.status), true);
}

		checkStatus = rxDesc.status & (RDES0_ES | RDES0_LE);

		// rx byte
		if(!checkStatus && (rxDesc.status & RDES0_LS) && (rxDesc.status & RDES0_FS))
		{
			len = RDES0_FL_GET(rxDesc.status) - ETH_FCS_LEN; // without crc byte

			skb_put(pSkb,len);

			//TODO : IPC_OFFLOAD  ??? H/W Checksum

			// Network buffer post process
			pSkb->dev = pNetDev;
			pSkb->protocol = eth_type_trans(pSkb, pNetDev);

			napi_gro_receive(&pGmacDev->napi, pSkb);

			pNetDev->last_rx = jiffies;
			pNetStats->rx_packets++;
			pNetStats->rx_bytes += len;

		} else {	// ERROR
			pNetStats->rx_errors++;

			if(rxDesc.status & RDES0_OE) pNetStats->rx_over_errors++;
			if(rxDesc.status & RDES0_LC) pNetStats->collisions++;
			if(rxDesc.status & RDES0_RWT) pNetStats->rx_frame_errors++;
			if(rxDesc.status & RDES0_CE) pNetStats->rx_crc_errors++ ;
//			if(rxDesc.status & RDES0_DBE) pNetStats->rx_frame_errors++ ;
			if(rxDesc.status & RDES0_LE) pNetStats->rx_length_errors++;

			memset(pSkb->data, 0, pSkb->data_len);	// buffer init
			len = DESC_GET_SIZE1(rxDesc.length);

			goto __set_rx_qptr;
		}

		len = ETH_HLEN + ETH_DATA_LEN + ETH_FCS_LEN + NET_IP_ALIGN;
		len = (len + pGmacDev->bus_align - 1) & pGmacDev->bus_mask;
		len += pGmacDev->bus_align;

		pSkb = dev_alloc_skb(len);
		if (pSkb == NULL){
			DPRINTK_GMAC_ERROR("skb memory allocation failed!!\n");
			break;
		}

		len -= sdpGmac_skbAlign(pGmacDev, pSkb) & pGmacDev->bus_mask;


__set_rx_qptr:
		rxDmaAddr = dma_map_single(pGmacDev->pDev,
					   pSkb->data, len, DMA_FROM_DEVICE);

		CONVERT_DMA_QPTR(rxDesc, len, rxDmaAddr, pSkb, 0, 0);

		if(sdp_ring_push(pGmacDev, &pGmacDev->rx_ring, &rxDesc) < 0){
			dev_kfree_skb_any(pSkb);
			DPRINTK_GMAC_ERROR("Error set rx qptr\n");
			break;
		}
	}

	spin_lock_irq(&pGmacDev->lock);
	if(pGmacDev->is_rx_stop) {
		dev_printk(KERN_DEBUG, pGmacDev->pDev, "restart receive frame!: new free desc %d, rx head %lu, rx tail %lu, DMACur %u\n", nrChkDesc, pGmacDev->rx_ring.head, pGmacDev->rx_ring.tail, (pDmaReg->curHostRxDesc-pDmaReg->rxDescListAddr)/sizeof(DMA_DESC_T));
		/* rx ring is not full. start rx */
		pGmacReg->configuration |= B_RX_ENABLE;
		pGmacDev->is_rx_stop = false;
	}
	spin_unlock_irq(&pGmacDev->lock);

	DPRINTK_GMAC_FLOW("exit\n");
	return nrChkDesc;
}

static void
sdpGmac_abnormal_intr_status (const u32 intrStatus, struct net_device* const pNetDev)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	if(intrStatus & B_FATAL_BUS_ERROR) {
		DPRINTK_GMAC_ERROR("Fatal Bus Error: \n");

		if(intrStatus & B_ERR_DESC)
			DPRINTK_GMAC_ERROR("\tdesc access error\n");
		else
			DPRINTK_GMAC_ERROR("\tdata buffer access error\n");

		if(intrStatus & B_ERR_READ_TR)
			DPRINTK_GMAC_ERROR("\tread access error\n");
		else
			DPRINTK_GMAC_ERROR("\twrite access error\n");

		if(intrStatus & B_ERR_TXDMA)
			DPRINTK_GMAC_ERROR("\ttx dma error\n");
		else
			DPRINTK_GMAC_ERROR("\trx dmaerror\n");

		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_EARLY_TX) {
		DPRINTK_GMAC_ERROR("Early Tx Error\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_RX_WDOG_TIMEOUT) {
		DPRINTK_GMAC_ERROR("Rx WatchDog timeout Error\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_RX_STOP_PROC) {
		DPRINTK_GMAC_ERROR("Rx process stop\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_RX_BUF_UNAVAIL) {
		dev_printk(KERN_NOTICE, pGmacDev->pDev, "Rx buffer unavailable Error: int 0x%08x, rx head %lu, rx tail %lu, DMACur %u\n", intrStatus, pGmacDev->rx_ring.head, pGmacDev->rx_ring.tail, (pDmaReg->curHostRxDesc-pDmaReg->rxDescListAddr)/sizeof(DMA_DESC_T));
		pGmacDev->netStats.rx_dropped++;

		/* stop rx */
		if(pGmacDev->is_rx_stop == false) {
			pGmacDev->is_rx_stop = true;
			pGmacReg->configuration &= ~B_RX_ENABLE;
			dev_printk(KERN_DEBUG, pGmacDev->pDev, "stop receive frame!\n");
		}

		napi_schedule(&pGmacDev->napi);
	}

	if(intrStatus & B_RX_OVERFLOW) {
		pGmacDev->netStats.rx_over_errors++;
		DPRINTK_GMAC_ERROR("Rx overflow Error(intrStatus:0x%08x)\n", intrStatus);
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_TX_UNDERFLOW) {
		DPRINTK_GMAC_ERROR("Tx underflow Error\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_TX_JAB_TIMEOUT) {
		DPRINTK_GMAC_ERROR("Tx jabber timeout Error\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}

	if(intrStatus & B_TX_STOP_PROC) {
		DPRINTK_GMAC_ERROR("Tx process stop\n");
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
	}
}


static irqreturn_t sdpGmac_intrHandler(int irq, void * devId)
{
	struct net_device *pNetDev = (struct net_device*)devId;
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_MMC_T *pMmcReg = pGmacDev->pMmcBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	u32 intrStatus, maskedIntrStatus;
#if 0
	struct timeval tv_start, tv_end;

	do_gettimeofday(&tv_start);
#endif
	DPRINTK_GMAC_FLOW("called\n");

	if(unlikely(pNetDev == NULL)) {
		DPRINTK_GMAC_ERROR("Not register Network Device, please check System\n");
		return -SDP_GMAC_ERR;
	}

	if(unlikely(pGmacDev == NULL)) {
		DPRINTK_GMAC_ERROR("Not register SDP-GMAC, please check System\n");
		return -SDP_GMAC_ERR;
	}

	intrStatus = pDmaReg->status;

	/* use masked interrupts */
	maskedIntrStatus = intrStatus & (pDmaReg->intrEnable | ~B_INTR_ALL);

	if((maskedIntrStatus & B_INTR_ALL) == 0) {
		/* all pendig interrupt clear */
		pDmaReg->status = intrStatus;
		return IRQ_NONE;
	}


	// DMA Self Clear bit is check

	if(maskedIntrStatus & B_TIME_STAMP_TRIGGER) {
		DPRINTK_GMAC("INTR: time stamp trigger\n");
	}

	if(maskedIntrStatus & B_PMT_INTR) {
		// TODO: make pmt resume function
		DPRINTK_GMAC("INTR: PMT interrupt\n");
	}

	if(maskedIntrStatus & B_MMC_INTR) {
		// TODO: make reading mmc intr rx and tx register
		// this register is hidden in Datasheet
		DPRINTK_GMAC_ERROR("INTR: MMC rx: 0x%08x\n", pMmcReg->intrRx);
		DPRINTK_GMAC_ERROR("INTR: MMC tx: 0x%08x\n", pMmcReg->intrTx);
		DPRINTK_GMAC_ERROR("INTR: MMC ipc offload: 0x%08x\n", pMmcReg->mmcIpcIntrRx);
	}

	if(maskedIntrStatus & B_LINE_INTF_INTR) {
		DPRINTK_GMAC("INTR: line interface interrupt\n");
	}

	//for NAPI, Tx, Rx process
	if(maskedIntrStatus & (B_RX_INTR | B_TX_INTR)) {
		//first. disable tx, rx interrupt.
		pDmaReg->intrEnable &= ~(B_RX_INTR | B_TX_INTR);
		napi_schedule(&pGmacDev->napi);
	}

// sample source don't support Early Rx, Tx
	if(maskedIntrStatus & B_EARLY_RX) {

	}

	if(maskedIntrStatus & B_TX_BUF_UNAVAIL) {

	}

// ABNORMAL Interrupt
	if(maskedIntrStatus & B_ABNORM_INTR_SUM) {
		sdpGmac_abnormal_intr_status (maskedIntrStatus, pNetDev);
	}

	pDmaReg->status = intrStatus;	// Clear interrupt pending register

	DPRINTK_GMAC_FLOW("exit\n");
#if 0
	do_gettimeofday(&tv_end);

	{
		unsigned long us;

		us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
			     (tv_end.tv_usec - tv_start.tv_usec);

		dev_printk(KERN_DEBUG, pGmacDev->pDev,
			"GMAC ISR process time %3luus (intr 0x%08x)\n",
			us, maskedIntrStatus);
	}
#endif
	return IRQ_HANDLED;
}

enum hrtimer_restart
sdp_mac_polling_callback(struct hrtimer *timer) {
	SDP_GMAC_DEV_T* pGmacDev = container_of(timer, SDP_GMAC_DEV_T, polling_timer);
	struct net_device *pNetDev = pGmacDev->pNetDev;
	sdpGmac_intrHandler((int)pNetDev->irq, pNetDev);
	hrtimer_start(&pGmacDev->polling_timer, ns_to_ktime(pGmacDev->polling_interval_us*1000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static int sdp_mac_regset(struct sdp_mac_reg_set *regset) {
	u8 * __iomem iomem;

	if(!regset) {
		pr_err("sdp_mac_regset: regset is NULL!!\n");
		return -EINVAL;
	}

	iomem = ioremap_nocache((u32)regset->addr, sizeof(u32));
	if(iomem) {
		writel( (readl(iomem)&~(regset->mask)) | (regset->value&regset->mask), iomem);
		wmb(); udelay(1);
		printk(KERN_DEBUG
			"sdp_mac_regset 0x%p - 0x%08llx, mask 0x%08llx, val 0x%08x, readl 0x%08x\n",
			regset, (u64)regset->addr, (u64)regset->mask, regset->value, readl(iomem));
		iounmap(iomem);
		return 0;
	} else {
		pr_err("sdp_mac_regset: ioremap failed!! addr:0x%08llx\n", (u64)regset->addr);
		return -ENOMEM;
	}
}


static int
sdpGmac_pad_hw_reset(SDP_GMAC_DEV_T* pGmacDev)
{
	struct sdp_gmac_plat *plat = pGmacDev->plat;
	struct phy_device *phydev = phy_find_first(pGmacDev->mdiobus);
	int retVal = SDP_GMAC_OK;
	int i;

#ifdef CONFIG_OF
#define PHYID_RTL8211 0x001cc910
	if(phydev != NULL) {
		if((phydev->phy_id&0x001ffff0) == PHYID_RTL8211) {
			dev_info(pGmacDev->pDev, "MAC Reset for RGMII Mode!!\n");
			for(i = 0; i < plat->select_rgmii.list_num; i++) {
				sdp_mac_regset(&plat->select_rgmii.list[i]);
			}
		}
	} else if(plat->fixed_phy_added && plat->fixed_rgmii) {
		dev_info(pGmacDev->pDev, "MAC Reset for RGMII Mode!!\n");
		for(i = 0; i < plat->select_rgmii.list_num; i++) {
			sdp_mac_regset(&plat->select_rgmii.list[i]);
		}
	}

	for(i = 0; i < plat->padctrl.list_num; i++) {
		sdp_mac_regset(&plat->padctrl.list[i]);
	}
#else
	/* chip dependent init code 0.969 */
	if(!pGmacDev->plat->init) {
		dev_warn(pGmacDev->pDev, "Board initialization code is not available!\n");
	} else {
		int init_ret = 0;
		/* call init code */
		init_ret = pGmacDev->plat->init();
		if(init_ret < 0) {
			dev_err(pGmacDev->pDev, "failed to board initialization!!(%d)\n", init_ret);
			return init_ret;
		}
	}
#endif

	return retVal;
}

static int
sdpGmac_reset(struct net_device *pNetDev)
{
	int i;

	SDP_GMAC_DEV_T* pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T * pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_DMA_T * pDmaReg = pGmacDev->pDmaBase;

/* version check */
	switch(pGmacReg->version & 0xFF) {
		case GMAC_SYNOP_VER:
		case (0x36):		// sdp1103, sdp1106
		case (0x37):		// sdp12xx
			DPRINTK_GMAC("Find SDP GMAC ver %04x\n", pGmacReg->version);
			break;
        default:
			DPRINTK_GMAC_ERROR("Can't find GMAC!!!!\n");
			return -SDP_GMAC_ERR;
			break;
	}

/* sdpGmac Reset */
// DMA Reset
	pDmaReg->busMode = B_SW_RESET; // '0x1'

	udelay(5);	// need 3 ~ 5us
	for(i = 1; pDmaReg->busMode & B_SW_RESET; i++) {
		if(i > 10000) {
			DPRINTK_GMAC_ERROR("GMAC Reset Failed!!!(waited 100us * 10000times)\n");
			DPRINTK_GMAC("plz check phy clock output register 0x19(0x1E40)\n");
			return -SDP_GMAC_ERR;
		}
		udelay(100);
		DPRINTK_GMAC("Retry GMAC Reset(%d)\n", i);
	}
	DPRINTK_GMAC("DMA reset is release(normal status)\n");


// all interrupt disable
	pGmacReg->intrMask = 0x60F;/* all disable */
	pGmacReg->interrupt = pGmacReg->interrupt; // clear status register
	pDmaReg->intrEnable = 0;
	pDmaReg->status = B_INTR_ALL;  // clear status register

// function define
	pGmacDev->oldlink = 0;
	pGmacDev->hwSpeed = 0;	// must same to init configuration register
	pGmacDev->hwDuplex = 0;		   	// must same to init configuration register
	pGmacDev->msg_enable = NETIF_MSG_LINK;
	pGmacDev->has_gmac = !!(pGmacDev->pDmaBase->hwfeature|B_GMIISEL);


	return 0;
}

static int
sdpGmac_getMacAddr(struct net_device *pNetDev, u8* pMacAddr)
{
	int retVal = SDP_GMAC_OK;

	SDP_GMAC_DEV_T* pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T * pGmacReg = pGmacDev->pGmacBase;

	u32 macAddr[2] = {0, 0};

	DPRINTK_GMAC_FLOW ("called\n");

	spin_lock_bh(&pGmacDev->lock);

	macAddr[0] = pGmacReg->macAddr_00_Low;
	macAddr[1] = pGmacReg->macAddr_00_High & 0xFFFF;

	spin_unlock_bh(&pGmacDev->lock);

	DPRINTK_GMAC_DBG("macAddrLow is 0x%08x\n", macAddr[0]);
	DPRINTK_GMAC_DBG ("macAddrHigh is 0x%08x\n", macAddr[1]);
	memcpy(pMacAddr, macAddr, N_MAC_ADDR);

	if(macAddr[0] == 0 && macAddr[1] == 0)	retVal = -SDP_GMAC_ERR;
	else if ((macAddr[0] == 0xFFFFFFFF) &&
		 (macAddr[1] == 0x0000FFFF)) retVal = -SDP_GMAC_ERR;

	DPRINTK_GMAC_FLOW ("exit\n");
	return retVal;

}

static void
sdpGmac_setMacAddr(struct net_device * pNetDev, const u8* pMacAddr)
{
	u32 macAddr[2] = {0, 0};
	SDP_GMAC_DEV_T* pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T * pGmacReg = pGmacDev->pGmacBase;

	DPRINTK_GMAC_FLOW ("called\n");

	memcpy(macAddr, pMacAddr, N_MAC_ADDR);

	spin_lock_bh(&pGmacDev->lock);

	/* sequence is important. 20120420 dongseok lee */
	pGmacReg->macAddr_00_High = macAddr[1];
	wmb();
	udelay(1);
	pGmacReg->macAddr_00_Low = macAddr[0];
	wmb();

	spin_unlock_bh(&pGmacDev->lock);

	DPRINTK_GMAC_FLOW ("exit\n");
}

static void
sdpGmac_dmaInit(struct net_device * pNetDev)
{
	SDP_GMAC_DEV_T* pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_DMA_T * pDmaReg = pGmacDev->pDmaBase;
	DPRINTK_GMAC_FLOW ("called\n");

	spin_lock_bh(&pGmacDev->lock);

	/*set desc base address! */
	pDmaReg->rxDescListAddr = 0;
	pDmaReg->txDescListAddr = 0;
	smp_wmb();
	pDmaReg->rxDescListAddr = (u32)pGmacDev->rxDescDma;
	pDmaReg->txDescListAddr = (u32)pGmacDev->txDescDma;

	pDmaReg->busMode = B_FIX_BURST_EN | B_BURST_LEN(8) |
		B_DESC_SKIP_LEN(((sizeof(DMA_DESC_T)-16+pGmacDev->bus_align-1)/pGmacDev->bus_align));
	DPRINTK_GMAC_DBG ("busMode set %08x\n", pDmaReg->busMode);

	pDmaReg->operationMode = B_TX_STR_FW | B_TX_THRESHOLD_192;
	DPRINTK_GMAC_DBG ("opMode set %08x\n", pDmaReg->operationMode);

	spin_unlock_bh(&pGmacDev->lock);

	DPRINTK_GMAC_FLOW ("exit\n");
}

static void
sdpGmac_gmacInit(struct net_device * pNetDev)
{
	SDP_GMAC_DEV_T* pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T * pGmacReg = pGmacDev->pGmacBase;

	u32 regVal = 0;

	DPRINTK_GMAC_FLOW ("called\n");

	spin_lock_bh(&pGmacDev->lock);

	// wd enable // jab enable
	// frame burst enable
#ifdef CONFIG_SDP_GMAC_GIGA_BIT
	regVal = B_FRAME_BURST_EN;  	// only GMAC, not support 10/100
#else
	regVal = B_PORT_MII;		// Port select mii
#endif
	regVal |= B_SPEED_100M;
	regVal |= B_DUPLEX_FULL;
	// jumbo frame disable // rx own enable // loop back off // retry enable
	// pad crc strip disable // back off limit set 0 // deferral check disable
	pGmacReg->configuration = regVal;
	DPRINTK_GMAC ("configuration set %08x\n", regVal);

// frame filter disable
#if 0
//	regVal = B_RX_ALL;
	// set pass control -> GmacPassControl 0 // broadcast enable
	// src addr filter disable // multicast disable  ????  // promisc disable
	// unicast hash table filter disable
//	pGmacReg->frameFilter = regVal;
	DPRINTK_GMAC ("frameFilter set %08x\n", regVal);
#endif

	spin_unlock_bh(&pGmacDev->lock);

	DPRINTK_GMAC_FLOW ("exit\n");
}

static struct net_device_stats *
sdpGmac_netDev_getStats(struct net_device *pNetDev)
{
	struct net_device_stats *pRet;
	SDP_GMAC_DEV_T * pGmacDev = netdev_priv(pNetDev);

	DPRINTK_GMAC_FLOW("called\n");

	pRet = &pGmacDev->netStats;

	DPRINTK_GMAC_FLOW("exit\n");
	return pRet;
}


static int
sdpGmac_netDev_hardStartXmit(struct sk_buff *pSkb, struct net_device *pNetDev)
{
	int retVal = SDP_GMAC_OK;

	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	dma_addr_t txDmaAddr;
	DMA_DESC_T txDesc;

	int retry = 1;

	DPRINTK_GMAC_FLOW ("called\n");

	if(unlikely(pSkb == NULL)) {
		DPRINTK_GMAC_ERROR("xmit skb is NULL!!!\n");
		retVal = -SDP_GMAC_ERR;
		goto __xmit_out;
	}

	while(sdp_ring_is_full(pGmacDev, &pGmacDev->tx_ring)) {
		if (!netif_queue_stopped(pNetDev)) {
			netif_stop_queue(pNetDev);
		}
		if (netif_msg_tx_queued(pGmacDev)) {
			dev_printk(KERN_DEBUG, pGmacDev->pDev, "total retry %d times. tx desc is Full.\n", retry);
		}
		retVal = NETDEV_TX_BUSY;
		goto __xmit_out;
	}

	if(pSkb->ip_summed == CHECKSUM_PARTIAL){
		// TODO:
		DPRINTK_GMAC_ERROR("H/W Checksum?? Not Implemente yet\n");
	}

	txDmaAddr = dma_map_single(pGmacDev->pDev, pSkb->data,
					 pSkb->len, DMA_TO_DEVICE);

			//        length,  buffer1, data1, buffer2, data2
	CONVERT_DMA_QPTR(txDesc, pSkb->len, txDmaAddr, pSkb, 0, 0 );

#ifdef CONFIG_SDP_MAC_NORMAL_DESC
	txDesc.status = TDES0_OWN;
	txDesc.length |= TDES1_FS | TDES1_LS | TDES1_IC;
#else
	txDesc.status = TDES0_OWN | TDES0_FS | TDES0_LS | TDES0_IC;
#endif

	while(sdp_ring_push(pGmacDev, &pGmacDev->tx_ring, &txDesc) < 0) {
		if(retry > 0){
			retry--;
			udelay(300);		// 100MBps 1 packet 1515 byte -> 121.20 us
			continue;
		}

		DPRINTK_GMAC_ERROR("Set Tx Descriptor is Failed\n");
		DPRINTK_GMAC_ERROR("No more Free Tx Descriptor\n");

		pGmacDev->netStats.tx_errors++;
		pGmacDev->netStats.tx_dropped++;
		dev_kfree_skb(pSkb);
		goto __xmit_out;
	}

	/* start tx */
	pDmaReg->txPollDemand = 0;
	pNetDev->trans_start = jiffies;

__xmit_out:

	DPRINTK_GMAC_FLOW ("exit\n");
	return retVal;
}


static void
sdp_mac_adjust_link(struct net_device *dev)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(dev);
	SDP_GMAC_DMA_T 	*pDmaReg = pGmacDev->pDmaBase;
	SDP_GMAC_T 	*pGmacReg = pGmacDev->pGmacBase;
	struct phy_device *phydev = dev->phydev;
	int new_state = 0;

	if (phydev == NULL)
		return;

	spin_lock_bh(&pGmacDev->lock);

	if (netif_running(dev) && phydev->link) {

		u32 ctrl = pGmacReg->configuration;
		wmb();

		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != pGmacDev->hwDuplex) {
			new_state = 1;
			if (!(phydev->duplex))
				ctrl &= ~B_DUPLEX_FULL;
			else
				ctrl |= B_DUPLEX_FULL;
			pGmacDev->hwDuplex = phydev->duplex;
		}
		/* Flow Control operation */
		if ((pGmacDev->flow_ctrl != SDP_MAC_FLOW_OFF) && phydev->pause)
			dev_warn(&dev->dev, "Not support Flow Control\n");

		if (phydev->speed != pGmacDev->hwSpeed) {
			new_state = 1;
			switch (phydev->speed) {
			case 1000:
				if (likely(pGmacDev->has_gmac))
					ctrl &= ~B_PORT_MII;
				break;
			case 100:
			case 10:
				if (pGmacDev->has_gmac) {
					ctrl |= B_PORT_MII;
					if (phydev->speed == SPEED_100) {
						ctrl |= B_SPEED_100M;
					} else {
						ctrl &= ~B_SPEED_100M;
					}
				} else {
					ctrl &= ~B_PORT_MII;
				}
				break;
			default:
				if (netif_msg_link(pGmacDev))
					dev_warn(&dev->dev, "Speed (%d) is not 10"
				       " or 100 or 1000!\n", phydev->speed);
				break;
			}

			pGmacDev->hwSpeed = phydev->speed;
		}

		if (!pGmacDev->oldlink) {
			new_state = 1;
			pGmacDev->oldlink = 1;

			if(dev->irq <= 0) {
				hrtimer_start(&pGmacDev->polling_timer, ns_to_ktime(pGmacDev->polling_interval_us*1000), HRTIMER_MODE_REL);
			}
		}

		/* change MAC speed setting! */
		if(new_state) {
			pGmacReg->configuration = ctrl & ~(B_RX_ENABLE|B_TX_ENABLE);
			wmb();
			pGmacReg->configuration |= (B_TX_ENABLE);
			wmb();
			pGmacReg->configuration |= (B_RX_ENABLE);
			wmb();
//			pDmaReg->operationMode |= (B_TX_EN);
//			wmb();
		}
	} else if (pGmacDev->oldlink) {
		new_state = 1;
		pGmacDev->oldlink = 0;
		pGmacDev->hwSpeed = 0;
		pGmacDev->hwDuplex= 0;

//		pDmaReg->operationMode &= ~(B_TX_EN);
//		wmb();
		pGmacReg->configuration &= ~(B_RX_ENABLE);
		wmb();

		//flush tx fifo
		pDmaReg->operationMode |= (1<<20);
		while(pDmaReg->operationMode & (1<<20));

		pGmacReg->configuration &= ~(B_TX_ENABLE);
		wmb();

		if(dev->irq <= 0) {
			hrtimer_cancel(&pGmacDev->polling_timer);
		}
	}

	spin_unlock_bh(&pGmacDev->lock);

	if (netif_running(dev) && new_state && netif_msg_link(pGmacDev))
		phy_print_status(phydev);

}

static int
sdpGmac_netDev_open (struct net_device *pNetDev)
{
	int retVal = SDP_GMAC_OK;
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_MMC_T *pMmcReg = pGmacDev->pMmcBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	struct sk_buff *pSkb;
	dma_addr_t  	rxDmaAddr;
	DMA_DESC_T	rxInitDesc;

	struct phy_device *phydev = pGmacDev->phydev;
	phy_interface_t phyinterface = sdp_mac_get_interface(pNetDev);

	u32 len;

	DPRINTK_GMAC_FLOW("%s: open called\n", pNetDev->name);

	if(!is_valid_ether_addr(pNetDev->dev_addr)) {
		dev_err(&pNetDev->dev, "no valid ethernet hw addr\n");
		return -EINVAL;
	}

	


	/* set currnet phydev */
	BUG_ON(phydev == NULL);

	/* Stop Advertising 1000BASE Capability if interface is not GMII */
	if ((phyinterface == PHY_INTERFACE_MODE_MII) ||
	    (phyinterface == PHY_INTERFACE_MODE_RMII))
		phydev->advertising &= ~(u32)(SUPPORTED_1000baseT_Half |
					 SUPPORTED_1000baseT_Full);


	sdpGmac_dmaInit(pNetDev);

	if(sdp_ring_set_index(pGmacDev, &pGmacDev->rx_ring, 0) < 0) {
		dev_err(pGmacDev->pDev, "rx sdp_ring_set_index return error!\n");
	}

	if(sdp_ring_set_index(pGmacDev, &pGmacDev->tx_ring, 0) < 0) {
		dev_err(pGmacDev->pDev, "tx sdp_ring_set_index return error!\n");
	}

	len = ETH_HLEN + ETH_DATA_LEN + ETH_FCS_LEN + NET_IP_ALIGN;
	len = (len + pGmacDev->bus_align - 1) & pGmacDev->bus_mask;
	len += pGmacDev->bus_align;

	dev_dbg(pGmacDev->pDev, "rx skb alloc len is %d\n", len);

	while(!sdp_ring_is_full(pGmacDev, &pGmacDev->rx_ring)) {
		pSkb = dev_alloc_skb(len);

		if(pSkb == NULL){
            DPRINTK_GMAC_ERROR("can't allocate sk buffer \n");
            break;
		}

		len -= sdpGmac_skbAlign(pGmacDev, pSkb) & pGmacDev->bus_mask;

		rxDmaAddr = dma_map_single(pGmacDev->pDev, pSkb->data,
					 len, DMA_FROM_DEVICE);

		CONVERT_DMA_QPTR(rxInitDesc, len, rxDmaAddr, pSkb, 0, 0);

		if(sdp_ring_push(pGmacDev, &pGmacDev->rx_ring, &rxInitDesc) < 0){
			dev_kfree_skb(pSkb);
			break;
		}
	}

//	pGmacReg->configuration |= B_JUMBO_FRAME_EN ;
	pGmacReg->configuration |= B_WATCHDOG_DISABLE ;

	//  MMC interrupt disable all
	//  TODO: check this block what use
	pMmcReg->intrMaskRx = 0x00FFFFFF;  // disable
	pMmcReg->intrMaskTx = 0x01FFFFFF;  // disable
	pMmcReg->mmcIpcIntrMaskRx = 0x3FFF3FFF;  // disable

	//  interrupt enable all
	pDmaReg->intrEnable = B_INTR_ENABLE_ALL;

	// tx, rx enable
	pGmacReg->configuration |= B_TX_ENABLE | B_RX_ENABLE;
	pDmaReg->operationMode |= B_TX_EN | B_RX_EN ;
	pGmacDev->is_rx_stop = false;

	//napi enable
	napi_enable(&pGmacDev->napi);

	netif_start_queue(pNetDev);

	if(pNetDev->irq > 0) {
		enable_irq(pNetDev->irq);
	}

	DPRINTK_GMAC_FLOW("%s: exit\n", pNetDev->name);

	return retVal;
}

static int
sdpGmac_netDev_close (struct net_device *pNetDev)
{
	int retVal = SDP_GMAC_OK;
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	DPRINTK_GMAC_FLOW("%s: close called\n", pNetDev->name);

	if(pNetDev->irq > 0) {
		disable_irq(pNetDev->irq);
	}

	netif_stop_queue(pNetDev);
	netif_carrier_off(pNetDev);

	/* napi */
	napi_disable(&pGmacDev->napi);

	// rx, tx disable
	pDmaReg->operationMode &= ~(B_TX_EN | B_RX_EN) ;
	pGmacReg->configuration &= ~(B_TX_ENABLE | B_RX_ENABLE);
	pGmacDev->is_rx_stop = true;

	// all interrupt disable
	pDmaReg->intrEnable = 0;
	pDmaReg->status = pDmaReg->status;	// Clear interrupt pending register

	// skb control
	sdp_ring_clear(pGmacDev, &pGmacDev->rx_ring);
	sdp_ring_clear(pGmacDev, &pGmacDev->tx_ring);

	DPRINTK_GMAC_FLOW("%s: close exit\n", pNetDev->name);
	return retVal;
}

static void
sdpGmac_netDev_setMulticastList (struct net_device *pNetDev)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;

	u32 frameFilter;

	DPRINTK_GMAC_FLOW("called\n");

	spin_lock_bh(&pGmacDev->lock);
	frameFilter = pGmacReg->frameFilter;
	spin_unlock_bh(&pGmacDev->lock);

	frameFilter &= ~( B_PROMISCUOUS_EN |
			  B_PASS_ALL_MULTICAST |
			  B_HASH_PERFECT_FILTER |
			  B_HASH_MULTICAST );

	if (pNetDev->flags & IFF_PROMISC) {
		PRINTK_GMAC ("%s: PROMISCUOUS MODE\n", pNetDev->name);
		frameFilter |= B_PROMISCUOUS_EN;
	}

#ifdef CONFIG_ARCH_SDP1002
	else if(pNetDev->flags & IFF_ALLMULTI ||
			(netdev_mc_count(pNetDev) > 14)) {
#else
	else if(pNetDev->flags & IFF_ALLMULTI ||
			(netdev_mc_count(pNetDev) > 15)) {
#endif
		DPRINTK_GMAC_FLOW("%s: PASS ALL MULTICAST \n", pNetDev->name);
		frameFilter |= B_PASS_ALL_MULTICAST;
	}

	else if(netdev_mc_count(pNetDev)){

		int i;
		struct netdev_hw_addr *pred;
		volatile u32 *mcRegHigh = &pGmacReg->macAddr_01_High;
		volatile u32 *mcRegLow = &pGmacReg->macAddr_01_Low;

		DPRINTK_GMAC_FLOW ("%s: HASH MULTICAST\n",
					pNetDev->name);

		// clear mc list
		i = 15;
		while (i--) {
			if(*mcRegHigh == 0) break;

			spin_lock_bh(&pGmacDev->lock);
			*mcRegHigh = 0;
			wmb();
			udelay(1);
			*mcRegLow = 0;
			wmb();
			spin_unlock_bh(&pGmacDev->lock);

			mcRegLow += 2;
			mcRegHigh += 2;
		}

		// set
		mcRegHigh = &pGmacReg->macAddr_01_High;
		mcRegLow = &pGmacReg->macAddr_01_Low;

		netdev_hw_addr_list_for_each(pred, &pNetDev->mc) {
			u32 mcValHigh;
			u32 mcValLow;

			DPRINTK_GMAC_FLOW("%s: cur_addr is %d.%d.%d.%d.%d.%d \n", pNetDev->name,
						pred->addr[0], pred->addr[1],
						pred->addr[2], pred->addr[3],
						pred->addr[4], pred->addr[5] );

			if(!(*pred->addr & 1)) continue;

			//mcValLow = (const u32*)(pred->addr);
			//mcValHigh = mcValLow + 1;	// u32 pointer
			memcpy(&mcValLow, pred->addr, 4);
			memcpy(&mcValHigh, ((u8*)pred->addr)+4, 2);

			spin_lock_bh(&pGmacDev->lock);
			*mcRegHigh = 0x80000000 | (mcValHigh & 0xFFFF);
			wmb();
			udelay(1);
			*mcRegLow = mcValLow;
			wmb();
			spin_unlock_bh(&pGmacDev->lock);

			mcRegHigh += 2;
			mcRegLow += 2;

		}
	}
	else {	// clear
		int i;
		volatile u32 *mcRegHigh = &pGmacReg->macAddr_01_High;
		volatile u32 *mcRegLow = &pGmacReg->macAddr_01_Low;

		// clear mc list
		i = 15;
		while (i--) {
			if(*mcRegHigh == 0) break;

			spin_lock_bh(&pGmacDev->lock);
			*mcRegHigh = 0;
			wmb();
			udelay(1);
			*mcRegLow = 0;
			wmb();
			spin_unlock_bh(&pGmacDev->lock);

			mcRegLow += 2;
			mcRegHigh += 2;
		}

	}


	spin_lock_bh(&pGmacDev->lock);
	pGmacReg->frameFilter = frameFilter;
	spin_unlock_bh(&pGmacDev->lock);

	DPRINTK_GMAC_FLOW("exit\n");
	return;
}


static int
sdpGmac_netDev_setMacAddr (struct net_device *pNetDev, void *pEthAddr)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	int retVal = SDP_GMAC_OK;

	struct sockaddr *addr = pEthAddr;
	u8 applied_addr[ETH_ALEN];

	DPRINTK_GMAC_FLOW("called\n");

	if(!is_valid_ether_addr(addr->sa_data)) {
		dev_err(pGmacDev->pDev, "MAC Address is not valid! requested %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr->sa_data[0], addr->sa_data[1], addr->sa_data[2], addr->sa_data[3], addr->sa_data[4], addr->sa_data[5]);

		//return -EADDRNOTAVAIL;
	}

	sdpGmac_setMacAddr(pNetDev, (const u8*)addr->sa_data);
	sdpGmac_getMacAddr(pNetDev, (u8*)applied_addr);

	if( memcmp(addr->sa_data, applied_addr, ETH_ALEN) != 0 ) {
		dev_warn(pGmacDev->pDev, "MAC Address is not match! requested %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr->sa_data[0], addr->sa_data[1], addr->sa_data[2], addr->sa_data[3], addr->sa_data[4], addr->sa_data[5]);
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
		return -EIO;
	}

	PRINTK_GMAC ("%s: Ethernet address %02x:%02x:%02x:%02x:%02x:%02x\n",
		     pNetDev->name, *applied_addr, *(applied_addr+1), *(applied_addr+2),
				*(applied_addr+3), *(applied_addr+4), *(applied_addr+5));

	memcpy(pNetDev->dev_addr, applied_addr, ETH_ALEN);

	DPRINTK_GMAC_FLOW("exit\n");
	return retVal;
}

static int
sdpGmac_netDev_ioctl (struct net_device *pNetDev, struct ifreq *pRq, int cmd)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	struct phy_device *phydev = pGmacDev->phydev;
	int retVal = SDP_GMAC_OK;

	DPRINTK_GMAC_FLOW("called\n");

	if(!netif_running(pNetDev)) {
		switch(cmd) {
			/* only allow mdio cmds */
			case SIOCGMIIPHY:
			case SIOCGMIIREG:
			case SIOCSMIIREG:
				break;
			default:
				return -EINVAL;
		}
	}

	if(phydev == NULL) {
		return -ENODEV;
	}

	retVal = phy_mii_ioctl(phydev, pRq, cmd);

	DPRINTK_GMAC_FLOW("exit\n");
	return retVal;
}


/*
 *  sdpGmac poll func for NAPI
 *  @napi : pointer to the napi structure.
 *  @budget : maximum number of packets that the current CPU can receive from
 *	      all interfaces.
 *  Description :
 *   This function implements the the reception process.
 *   Also it runs the TX completion thread
 */
static int sdpGmac_netDev_poll(struct napi_struct *napi, int budget)
{
	SDP_GMAC_DEV_T *pGmacDev = container_of(napi, SDP_GMAC_DEV_T, napi);
	int work_done = 0;

	dev_dbg(pGmacDev->pDev, "CPU%d, sdpGmac_netDev_poll Enter!\n", smp_processor_id());

	sdpGmac_netDev_tx(pGmacDev->pNetDev, budget);

	work_done = sdpGmac_netDev_rx(pGmacDev->pNetDev, budget);

	if (work_done < budget) {
		napi_complete(napi);
		pGmacDev->pDmaBase->intrEnable |= (B_RX_INTR | B_TX_INTR);
		dev_dbg(pGmacDev->pDev, "CPU%d, NAPI Complete workdone=%d\n", smp_processor_id(), work_done);
	}

	dev_dbg(pGmacDev->pDev, "CPU%d, sdpGmac_netDev_poll Exit!\n", smp_processor_id());

	return work_done;
}


#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled. */
static void
sdpGmac_netDev_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	sdpGmac_intrHandler((int)dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

// TODO:
#if 0
static int
sdpGmac_netDev_chgMtu(struct net_device *pNetDev, int newMtu)
{
	int retVal = SDP_GMAC_OK;

	DPRINTK_GMAC("called\n");

//  TODO

	DPRINTK_GMAC("exit\n");
	return retVal;
}


static void
sdpGmac_netDev_txTimeOut (struct net_device *pNetDev)
{
	DPRINTK_GMAC("called\n");

//  TODO

	DPRINTK_GMAC("exit\n");
	return;
}
#endif


/*
 * Ethernet Tool Support
 */
static int
sdpGmac_ethtool_getsettings (struct net_device *pNetDev, struct ethtool_cmd *pCmd)
{
	int retVal;

	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);

	DPRINTK_GMAC("called\n");

	if(pNetDev->phydev == NULL) {
		dev_err(pGmacDev->pDev, "PHY is not registered.\n");
		return -ENODEV;
	}

	pCmd->maxtxpkt = 1;	// ????
	pCmd->maxrxpkt = 1;	// ????

	retVal = phy_ethtool_gset(pNetDev->phydev, pCmd);

	DPRINTK_GMAC("exit\n");
	return retVal;
}

static int
sdpGmac_ethtool_setsettings (struct net_device *pNetDev, struct ethtool_cmd *pCmd)
{
	int retVal = SDP_GMAC_OK;

	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);

	DPRINTK_GMAC_FLOW("called\n");

	if(pNetDev->phydev == NULL) {
		dev_err(pGmacDev->pDev, "PHY is not registered.\n");
		return -ENODEV;
	}

	retVal = phy_ethtool_sset(pNetDev->phydev, pCmd);

	DPRINTK_GMAC_FLOW("exit\n");
	return retVal;
}

static void
sdpGmac_ethtool_getdrvinfo (struct net_device *pNetDev, struct ethtool_drvinfo *pDrvInfo)
{

	DPRINTK_GMAC("called\n");

	memset(pDrvInfo, 0, sizeof(*pDrvInfo));
	strncpy(pDrvInfo->driver, ETHER_NAME, sizeof(pDrvInfo->driver)-1);
	strncpy(pDrvInfo->version, GMAC_DRV_VERSION, sizeof(pDrvInfo->version)-1);
	strncpy(pDrvInfo->fw_version, "N/A", sizeof(pDrvInfo->fw_version)-1);
	strncpy(pDrvInfo->bus_info, dev_name(pNetDev->dev.parent), sizeof(pDrvInfo->bus_info)-1);

	DPRINTK_GMAC("exit\n");
	return;
}

static u32
sdpGmac_ethtool_getmsglevel (struct net_device *pNetDev)
{
	u32 retVal = SDP_GMAC_OK;

	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	DPRINTK_GMAC("called\n");

	retVal = pGmacDev->msg_enable;

	DPRINTK_GMAC("exit\n");
	return retVal;
}

static void
sdpGmac_ethtool_setmsglevel (struct net_device *pNetDev, u32 level)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	DPRINTK_GMAC("called\n");

	pGmacDev->msg_enable = level;

	DPRINTK_GMAC("exit\n");

	return;
}



// phy reset
static int
sdpGmac_ethtool_nwayreset (struct net_device *pNetDev)
{
	int retVal = SDP_GMAC_OK;
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);

	DPRINTK_GMAC("called\n");

	if(pNetDev->phydev == NULL) {
		dev_err(pGmacDev->pDev, "PHY is not registered.\n");
		return -ENODEV;
	}

	retVal = genphy_restart_aneg(pNetDev->phydev);

	DPRINTK_GMAC("exit\n");
	return retVal;
}


/* number of registers GMAC + MII */
static int
sdpGmac_ethtool_getregslen (struct net_device *pNetDev)
{
	int retVal = 0;

	DPRINTK_GMAC("called\n");

	retVal = (int)sizeof(SDP_GMAC_T);
	retVal += (int)sizeof(SDP_GMAC_MMC_T);
	retVal += (int)sizeof(SDP_GMAC_TIME_STAMP_T);
	retVal += (int)sizeof(SDP_GMAC_MAC_2ND_BLOCK_T);
	retVal += (int)sizeof(SDP_GMAC_DMA_T);
	retVal += (int)(32 << 2);  // MII address

	DPRINTK_GMAC("exit\n");

	return retVal;
}

/* get all registers value GMAC + MII */
static void
sdpGmac_ethtool_getregs (struct net_device *pNetDev, struct ethtool_regs *pRegs, void *pBuf)
{

	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	volatile u32 *pRegAddr;
	u32 *pData = (u32*) pBuf;
	u16 phyVal;
	unsigned int i, j = 0;

	DPRINTK_GMAC_FLOW("called\n");

	if(pNetDev->phydev == NULL) {
		dev_err(pGmacDev->pDev, "PHY is not registered.\n");
		return;
	}

	pRegAddr = (volatile u32*)pGmacDev->pGmacBase;
	for(i = 0; i < (sizeof(SDP_GMAC_T) >> 2); i++)
		pData[j++] = *pRegAddr++;

	pRegAddr = (volatile u32*)pGmacDev->pMmcBase;
	for(i = 0; i < (sizeof(SDP_GMAC_MMC_T) >> 2); i++)
		pData[j++] = *pRegAddr++;

	pRegAddr = (volatile u32*)pGmacDev->pTimeStampBase;
	for(i = 0; i < (sizeof(SDP_GMAC_TIME_STAMP_T) >> 2); i++)
		pData[j++] = *pRegAddr++;

	pRegAddr = (volatile u32*)pGmacDev->pDmaBase;
	for(i = 0; i < (sizeof(SDP_GMAC_DMA_T) >> 2); i++)
		pData[j++] = *pRegAddr++;

	for(i = 0; i < 32; i++){
		phyVal = (u16)phy_read(pNetDev->phydev, i);
		pData[j++] = phyVal & 0xFFFF;
	}

	DPRINTK_GMAC_FLOW("exit\n");

	return;
}

static char sdp_mac_ethtool_stats_string[][ETH_GSTRING_LEN] = {
	{"rx_packets"},
	{"tx_packets"},
	{"rx_bytes"},
	{"tx_bytes"},
	{"rx_errors"},
	{"tx_errors"},
	{"rx_dropped"},
	{"tx_dropped"},
	{"multicast"},
	{"collisions"},
	{"rx_length_errors"},
	{"rx_over_errors"},
	{"rx_crc_errors"},
	{"rx_frame_errors"},
	{"rx_fifo_errors"},
	{"rx_missed_errors"},
	{"tx_aborted_errors"},
	{"tx_carrier_errors"},
	{"tx_fifo_errors"},
	{"tx_heartbeat_errors"},
	{"tx_window_errors"},
	{"rx_compressed"},
	{"tx_compressed"},
};

static void
sdpGmac_ethtool_get_stats(struct net_device *pNetDev, struct ethtool_stats *stats, u64 *data)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);

	if(stats->n_stats < ARRAY_SIZE(sdp_mac_ethtool_stats_string))
	{
		dev_err(pGmacDev->pDev, "not support result len!\n");
		return;
	}

	data[0] = pNetDev->stats.rx_packets;
	data[1] = pNetDev->stats.tx_packets;
	data[2] = pNetDev->stats.rx_bytes;
	data[3] = pNetDev->stats.tx_bytes;
	data[4] = pNetDev->stats.rx_errors;
	data[5] = pNetDev->stats.tx_errors;
	data[6] = pNetDev->stats.rx_dropped;
	data[7] = pNetDev->stats.tx_dropped;
	data[8] = pNetDev->stats.multicast;
	data[9] = pNetDev->stats.collisions;
	data[10] = pNetDev->stats.rx_length_errors;
	data[11] = pNetDev->stats.rx_over_errors;
	data[12] = pNetDev->stats.rx_crc_errors;
	data[13] = pNetDev->stats.rx_frame_errors;
	data[14] = pNetDev->stats.rx_fifo_errors;
	data[15] = pNetDev->stats.rx_missed_errors;
	data[16] = pNetDev->stats.tx_aborted_errors;
	data[17] = pNetDev->stats.tx_carrier_errors;
	data[18] = pNetDev->stats.tx_fifo_errors;
	data[19] = pNetDev->stats.tx_heartbeat_errors;
	data[20] = pNetDev->stats.tx_window_errors;
	data[21] = pNetDev->stats.rx_compressed;
	data[22] = pNetDev->stats.tx_compressed;

	return;
}


enum sdp_mac_selftest_num {
	SDP_MAC_LB,
	SDP_EXT_LB,
	SDP_END_LB,
};
static char sdp_mac_ethtool_selftest_string[][ETH_GSTRING_LEN] = {
#if 0
	{"link status test"},
#endif
	{"mac internal LB test"},
	{"mac external LB test"},
};

static int
sdp_mac_ethtool_get_sset_count(struct net_device *pNetDev, int sset_id)
{
	if(sset_id == ETH_SS_TEST) {
		return ARRAY_SIZE(sdp_mac_ethtool_selftest_string);
	} else if(sset_id == ETH_SS_STATS) {
		return ARRAY_SIZE(sdp_mac_ethtool_stats_string);
	}
	return -ENOTSUPP;
}

static void
sdp_mac_ethtool_get_strings(struct net_device *pNetDev, u32 stringset, u8 *data)
{
	unsigned int i;
	if(stringset == ETH_SS_TEST) {
		for(i = 0; i < ARRAY_SIZE(sdp_mac_ethtool_selftest_string); i++) {
			strncpy(data+(i*ETH_GSTRING_LEN), sdp_mac_ethtool_selftest_string[i], ETH_GSTRING_LEN);
		}
	} else if(stringset == ETH_SS_STATS) {
		for(i = 0; i < ARRAY_SIZE(sdp_mac_ethtool_stats_string); i++) {
			strncpy(data+(i*ETH_GSTRING_LEN), sdp_mac_ethtool_stats_string[i], ETH_GSTRING_LEN);
		}
	}
}

static int
sdp_mac_selftest_setup_desc(SDP_GMAC_DEV_T *pGmacDev)
{
	struct sk_buff *pSkb = NULL;
	DMA_DESC_T dmaDesc;
	dma_addr_t dmaAddr;
	unsigned int alloc_len = ETH_FRAME_LEN + ETH_FCS_LEN + pGmacDev->bus_align;
	int err;
	unsigned int cnt;


	alloc_len += pGmacDev->bus_align - 1;
	alloc_len &= pGmacDev->bus_mask;

	//setup tx data
	pSkb = dev_alloc_skb(alloc_len);

	if(pSkb == NULL){
		return -ENOMEM;
	}

	skb_put(pSkb, ETH_DATA_LEN);

	memset(pSkb->data, 0x5A, pSkb->len);
	for(cnt = 0; cnt < pSkb->len; cnt++)
		pSkb->data[cnt] = (unsigned char)(cnt & 0xFF);

	dmaAddr = dma_map_single(pGmacDev->pDev, pSkb->data,
					pSkb->len, DMA_TO_DEVICE);

	CONVERT_DMA_QPTR(dmaDesc, pSkb->len, dmaAddr, pSkb, 0, 0 );

#ifdef CONFIG_SDP_MAC_NORMAL_DESC
	dmaDesc.status = TDES0_OWN;
	dmaDesc.length |= TDES1_FS | TDES1_LS | TDES1_IC;
#else
	dmaDesc.status = TDES0_OWN | TDES0_FS | TDES0_LS | TDES0_IC;
#endif

	err = sdp_ring_push(pGmacDev, &pGmacDev->tx_ring, &dmaDesc);
	if(err < 0) {
		dev_err(pGmacDev->pDev, "sdpGmac_setTxQptr error(%d)!\n", err);
	}


	//setup rx data
	pSkb = dev_alloc_skb(alloc_len);

	if(pSkb == NULL){
		return -ENOMEM;
	}

	memset(pSkb->data, 0xA5, ETH_FRAME_LEN);

	dmaAddr = dma_map_single(pGmacDev->pDev, pSkb->data,
					 ETH_FRAME_LEN, DMA_FROM_DEVICE);

	CONVERT_DMA_QPTR(dmaDesc, ETH_FRAME_LEN, dmaAddr, pSkb, 0, 0 );
	err = sdp_ring_push(pGmacDev, &pGmacDev->rx_ring, &dmaDesc);
	if(err < 0) {
		dev_err(pGmacDev->pDev, "sdpGmac_setRxQptr error(%d)!\n", err);
	}

	return 0;
}

static int
sdp_mac_selftest_compare_frame(SDP_GMAC_DEV_T *pGmacDev, const char *header)
{
	struct sk_buff *pSkb_tx = NULL, *pSkb_rx = NULL;
	DMA_DESC_T dmaDesc_tx, dmaDesc_rx;
	int err;

	if(header == NULL) {
		header = "";
	}

	err = sdp_ring_pull(pGmacDev, &pGmacDev->tx_ring, &dmaDesc_tx);
	if(err < 0) {
		dev_err(pGmacDev->pDev, "tx no more frame\n");
		goto err0;
	}

	dev_dbg(pGmacDev->pDev, "get Tx frame!\n");
	pSkb_tx = (struct sk_buff *)dmaDesc_tx.data1;
	dma_unmap_single(pGmacDev->pDev, dmaDesc_tx.buffer1, pSkb_tx->len, DMA_TO_DEVICE);

	err = sdp_ring_pull(pGmacDev, &pGmacDev->rx_ring, &dmaDesc_rx);
	if(err < 0) {
		dev_err(pGmacDev->pDev, "rx no more frame\n");
		goto err0;
	}

	dev_dbg(pGmacDev->pDev, "get Rx frame!\n");
	pSkb_rx = (struct sk_buff *)dmaDesc_rx.data1;

	skb_put(pSkb_rx, RDES0_FL_GET(dmaDesc_rx.status));

	dma_unmap_single(pGmacDev->pDev, dmaDesc_rx.buffer1, RDES0_FL_GET(dmaDesc_rx.status), DMA_FROM_DEVICE);

	//print_hex_dump_bytes("Tx: ", DUMP_PREFIX_ADDRESS, pSkb_tx->data, pSkb_tx->len);
	//print_hex_dump_bytes("Rx: ", DUMP_PREFIX_ADDRESS, pSkb_rx->data, pSkb_rx->len);

	err = memcmp(pSkb_rx->data, pSkb_tx->data, pSkb_tx->len);
	if(err) {
		dev_err(pGmacDev->pDev, "%s data missmatch!!\n", header);
		dev_err(pGmacDev->pDev,
			"tx desc status %#x, rx desc status %#x, txlen %d rxlen %d\n",
			dmaDesc_tx.status, dmaDesc_rx.status, pSkb_tx->len, pSkb_rx->len);

		dev_err(pGmacDev->pDev, "Tx error(%s%s%s%s)\n",
			dmaDesc_tx.status&TDES0_LC?"LC ":"",
			dmaDesc_tx.status&TDES0_NC?"NC ":"",
			dmaDesc_tx.status&TDES0_LCO?"LCO ":"",
			dmaDesc_tx.status&TDES0_EC?"EC ":"");

		dev_err(pGmacDev->pDev, "Rx error(%s%s%s%s%s%s)\n",
			dmaDesc_rx.status&RDES0_LE ? "LE ":"",
			dmaDesc_rx.status&RDES0_OE ? "OE ":"",
			dmaDesc_rx.status&RDES0_LC ? "LC ":"",
			dmaDesc_rx.status&RDES0_RWT ? "RWT ":"",
			dmaDesc_rx.status&RDES0_DBE ? "DBE ":"",
			dmaDesc_rx.status&RDES0_CE ? "CE ":"");

		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);

		print_hex_dump(KERN_ERR, "Tx: ", DUMP_PREFIX_ADDRESS, 16, 1,
		       pSkb_tx->data, pSkb_tx->len, true);
		pr_err("\n");
		print_hex_dump(KERN_ERR, "Rx: ", DUMP_PREFIX_ADDRESS, 16, 1,
		       pSkb_rx->data, pSkb_rx->len, true);
		pr_err("\n");
		dev_err(pGmacDev->pDev, "phy addr txbuf: 0x%08x, rxbuf: 0x%08x\n",
			dmaDesc_tx.buffer1 , dmaDesc_rx.buffer1);
		pr_err("\n");
		err = -1000;
	}

	dev_kfree_skb(pSkb_tx);
	dev_kfree_skb(pSkb_rx);

	return err;

err0:
	return -ENODATA;
}

/* return xfer time(us) */
static int
sdp_mac_selftest_mac_xfer(SDP_GMAC_DEV_T *pGmacDev, const char *header)
{
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;

	unsigned long timeout_ms = 100;
	unsigned long timeout;
	int err = 0;

	static struct timeval start, finish;

	if(header == NULL) {
		header = "";
	}

	do_gettimeofday(&start);

	pDmaReg->status = pDmaReg->status;//all clear

	pGmacReg->frameFilter|= B_RX_ALL;
	pDmaReg->operationMode |= B_TX_EN | B_RX_EN;
	pGmacReg->configuration |= B_TX_ENABLE | B_RX_ENABLE;

	timeout = jiffies + msecs_to_jiffies(timeout_ms);
	while(!(pDmaReg->status&B_TX_INTR)) {
		if(time_is_before_jiffies(timeout)) {
			dev_err(pGmacDev->pDev, "%s Tx timeout! %lums\n", header, timeout_ms);
			err = -1010;
			goto done;
		}
	}
	pDmaReg->status = B_TX_INTR;//clear
	dev_dbg(pGmacDev->pDev, "%s Tx done!\n", header);

	timeout = jiffies + msecs_to_jiffies(timeout_ms);
	while(!(pDmaReg->status&B_RX_INTR)) {
		if(time_is_before_jiffies(timeout)) {
			dev_err(pGmacDev->pDev, "%s Rx timeout! %lums\n", header, timeout_ms);
			err = -1020;
			goto done;
		}
	}
	pDmaReg->status = B_RX_INTR;//clear
	dev_dbg(pGmacDev->pDev, "%s Rx done!\n", header);


done:
	pGmacReg->configuration &= ~(B_TX_ENABLE | B_RX_ENABLE);
	pDmaReg->operationMode &= ~(B_TX_EN | B_RX_EN);
	pGmacReg->frameFilter &= ~B_RX_ALL;

	do_gettimeofday(&finish);

	if(err >= 0) {
		err = (int)(timeval_to_ns(&finish) - timeval_to_ns(&start)) / NSEC_PER_USEC;
	} else {
		sdp_mac_gmac_dump(pGmacDev);
		sdp_mac_dma_dump(pGmacDev);
		dev_err(pGmacDev->pDev,
			"%s tx desc status %#x, rx desc status %#x\n",
			header, pGmacDev->pTxDesc[pGmacDev->tx_ring.tail].status, pGmacDev->pRxDesc[pGmacDev->rx_ring.tail].status);
	}

	return err;
}

static int
sdp_mac_selftest_do(struct net_device *pNetDev, enum sdp_mac_selftest_num test)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	struct phy_device test_phydev = *pGmacDev->phydev;
	struct phy_device *phydev = &test_phydev;
	int err = 0, ret = 0;
	int time;

	dev_dbg(pGmacDev->pDev, "%s...\n",
		sdp_mac_ethtool_selftest_string[test]);


	sdpGmac_dmaInit(pNetDev);
	sdp_ring_set_index(pGmacDev, &pGmacDev->rx_ring, 0);
	sdp_ring_set_index(pGmacDev, &pGmacDev->tx_ring, 0);
	sdp_mac_selftest_setup_desc(pGmacDev);

	if(phy_read(phydev, MII_BMCR) & BMCR_ANENABLE) {
		phydev->autoneg = AUTONEG_ENABLE;
	} else {
		phydev->autoneg = AUTONEG_DISABLE;
	}

	genphy_read_status(phydev);
	if(test == SDP_MAC_LB) pGmacReg->configuration |= B_LOOP_BACK_EN;
	mdelay(100);

	if(test != SDP_MAC_LB) {
		unsigned long timeout_ms = 1000;
		unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

		while(genphy_read_status(phydev) >= 0) {
			if(phydev->link == 1) {
				mdelay(50);
				break;
			}
			if(time_is_before_jiffies(timeout)) {
				dev_err(pGmacDev->pDev, "%s link timeout! %lums\n", sdp_mac_ethtool_selftest_string[test], timeout_ms);
				err = -ETIMEDOUT;
				break;
			}
		}
	}

	if(err == 0) {
		/* adjust speed */

		int bmcr = phy_read(phydev, MII_BMCR);
		u32 ctrl = pGmacDev->pGmacBase->configuration;
		wmb();


		if (bmcr < 0)
				dev_warn(pGmacDev->pDev, "sdp_mac_selftest_adjust_speed_mac: can't get BMCR!! %d\n", bmcr);

		if (phydev->duplex == DUPLEX_FULL)
				ctrl |= B_DUPLEX_FULL;
		else
				ctrl &= ~B_DUPLEX_FULL;

		if (phydev->speed == SPEED_1000)
			if (likely(pGmacDev->has_gmac))
				ctrl &= ~B_PORT_MII;
			else
				dev_warn(pGmacDev->pDev, "sdp_mac_selftest_adjust_speed_mac: 1000MBps Speed is not supported!!\n");
		else {
			ctrl |= B_PORT_MII;
			if (phydev->speed == SPEED_100)
					ctrl |= B_SPEED_100M;
			else
					ctrl &= ~B_SPEED_100M;
		}

		pGmacDev->pGmacBase->configuration = ctrl;
		wmb();

		dev_printk(KERN_DEBUG, pGmacDev->pDev, "%s: %s, speed %dMBps, %s duplex, %s BMCR:0x%04x, LPA:0x%04x\n",
			sdp_mac_ethtool_selftest_string[test], AUTONEG_ENABLE == phydev->autoneg?"AutoNeg":"Forced",
			phydev->speed, phydev->duplex == DUPLEX_FULL?"Full":"Half",
			phy_read(phydev, MII_BMCR)&BMCR_LOOPBACK?"PHY LB, ":"" , phy_read(phydev, MII_BMCR), phy_read(phydev, MII_LPA));
	}

	if(err == 0) {
		time = sdp_mac_selftest_mac_xfer(pGmacDev, sdp_mac_ethtool_selftest_string[test]);
	} else {
		time = err;
	}

	if(time < 0) {
		err = time;
	} else {
		err = sdp_mac_selftest_compare_frame(pGmacDev, sdp_mac_ethtool_selftest_string[test]);
	}

	if(err < 0) {
		sdp_mac_phy_regdump(pGmacDev, sdp_mac_ethtool_selftest_string[test]);
		dev_info(pGmacDev->pDev, "%s fail(%d)!!!\n",
			sdp_mac_ethtool_selftest_string[test], err);

		ret = err;
	} else {
		ret = time;
	}

	if(test == SDP_MAC_LB) pGmacReg->configuration &= ~(B_LOOP_BACK_EN);

	sdp_ring_clear(pGmacDev, &pGmacDev->rx_ring);
	sdp_ring_clear(pGmacDev, &pGmacDev->tx_ring);

	return ret;
}

static void
sdp_mac_ethtool_selftest(struct net_device *pNetDev, struct ethtool_test *test, u64 *data)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	bool if_running = netif_running(pNetDev);
	int err = 0;

	dev_dbg(pGmacDev->pDev, "Start self test!\n");

	if(test->len < ARRAY_SIZE(sdp_mac_ethtool_selftest_string)) {
		dev_err(pGmacDev->pDev, "not support result len!\n");
		test->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	memset(data, 0, sizeof(u64)*test->len);

	if(test->flags & ETH_TEST_FL_OFFLINE) {

		if( !pGmacDev->phydev ) {
			dev_err(pGmacDev->pDev, "Could not find PHY for selftest!\n");
			return;
		}

		//Start Offline test!!
		if (if_running) {
			/* indicate we're in test mode */
			//dev_close(pNetDev);
			sdpGmac_netDev_close(pNetDev);
		}

		dev_dbg(pGmacDev->pDev, "Offline test entered\n");

		//external loopback test
		if(test->flags & ETH_TEST_FL_EXTERNAL_LB) {
			test->flags |= ETH_TEST_FL_EXTERNAL_LB_DONE;

			err = sdp_mac_selftest_do(pNetDev, SDP_EXT_LB);
			if(err < 0) {
				test->flags |= ETH_TEST_FL_FAILED;
			}
			data[SDP_EXT_LB] = (u64)err;

		}

		if(err <= 0) {
			//MAC loopback test
			err = sdp_mac_selftest_do(pNetDev, SDP_MAC_LB);
			if(err < 0) {
				test->flags |= ETH_TEST_FL_FAILED;
			}
			data[SDP_MAC_LB] = (u64)err;
		}

		//Reset GMAC H/W
		if(sdpGmac_reset(pNetDev) < 0){
			dev_err(pGmacDev->pDev, "GMAC H/W Reset error(%d)!\n", err);
			test->flags |= ETH_TEST_FL_FAILED;
		} else {
			//register initalize
			sdpGmac_dmaInit(pNetDev);
			sdpGmac_gmacInit(pNetDev);
			sdp_ring_set_index(pGmacDev, &pGmacDev->rx_ring, 0);
			sdp_ring_set_index(pGmacDev, &pGmacDev->tx_ring, 0);
			if(is_valid_ether_addr(pNetDev->dev_addr)) {
				sdpGmac_setMacAddr(pNetDev, (const u8*)pNetDev->dev_addr);
			}
		}

		if (if_running) {
			//dev_open(pNetDev);
			sdpGmac_netDev_open(pNetDev);
		}
	}
	dev_dbg(pGmacDev->pDev, "End self test!\n");
}

#ifdef CONFIG_PM_DEBUG
#define PM_DEV_DBG(arg...)	dev_printk(KERN_DEBUG, arg)
#else
#define PM_DEV_DBG(arg...)	dev_dbg(arg)
#endif

#ifdef CONFIG_PM
static int sdpGmac_drv_suspend(struct device *dev)
{
	struct platform_device *pDev = to_platform_device(dev);
	struct net_device *pNetDev = platform_get_drvdata(pDev);
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	SDP_GMAC_POWER_T *pPower = &pGmacDev->power;

	PM_DEV_DBG(pGmacDev->pDev, "suspend\n");

	/* backup mac address */
	memcpy(pPower->gmac_macAddr, (void*)((u32)&pGmacReg->macAddr_00_High),
	 	sizeof(pPower->gmac_macAddr));

	if(netif_running(pNetDev))
		sdpGmac_netDev_close(pNetDev);

	return 0;
}


static int
sdpGmac_drv_resume(struct device *dev)
{
	struct platform_device *pDev = to_platform_device(dev);
	struct net_device *pNetDev = platform_get_drvdata(pDev);
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	SDP_GMAC_T *pGmacReg = pGmacDev->pGmacBase;
	//SDP_GMAC_DMA_T *pDmaReg = pGmacDev->pDmaBase;
	SDP_GMAC_POWER_T *pPower = &pGmacDev->power;
	int ret = 0;

	PM_DEV_DBG(pGmacDev->pDev, "resume\n");

	ret = sdpGmac_pad_hw_reset(pGmacDev);
	if(ret < 0)
		return ret;

	ret = sdpGmac_reset(pNetDev);
	if(ret < 0)
		return ret;

	sdpGmac_gmacInit(pNetDev);
	sdpGmac_dmaInit(pNetDev);

	/* restore mac address */
	memcpy((void *)((u32)&pGmacReg->macAddr_00_High), pPower->gmac_macAddr,
		sizeof(pPower->gmac_macAddr));

	if(netif_running(pNetDev))
		return sdpGmac_netDev_open(pNetDev);

	return 0;
}
#endif

static const struct ethtool_ops sdpGmac_ethtool_ops = {
	.get_settings	= sdpGmac_ethtool_getsettings,
	.set_settings	= sdpGmac_ethtool_setsettings,
	.get_drvinfo	= sdpGmac_ethtool_getdrvinfo,
	.get_msglevel	= sdpGmac_ethtool_getmsglevel,
	.set_msglevel	= sdpGmac_ethtool_setmsglevel,
	.nway_reset		= sdpGmac_ethtool_nwayreset,
	.get_link  		= ethtool_op_get_link,
	.get_regs_len	= sdpGmac_ethtool_getregslen,
	.get_regs		= sdpGmac_ethtool_getregs,
	.self_test		= sdp_mac_ethtool_selftest,
	.get_strings	= sdp_mac_ethtool_get_strings,
	.get_sset_count = sdp_mac_ethtool_get_sset_count,
	.get_ethtool_stats	= sdpGmac_ethtool_get_stats,
};

static const struct net_device_ops sdpGmac_netdev_ops = {
	.ndo_open				= sdpGmac_netDev_open,
	.ndo_stop               = sdpGmac_netDev_close,
	.ndo_start_xmit         = sdpGmac_netDev_hardStartXmit,
	.ndo_get_stats          = sdpGmac_netDev_getStats,
	.ndo_set_rx_mode        = sdpGmac_netDev_setMulticastList,
	.ndo_do_ioctl           = sdpGmac_netDev_ioctl,
	.ndo_change_mtu         = eth_change_mtu,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = sdpGmac_netDev_setMacAddr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= sdpGmac_netDev_poll_controller,
#endif
};


#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
static int
sdp_mac_dbg_print_show(struct seq_file *s, void *data)
{
	//seq_printf(s, "PRINT\n");
	return 0;
}

static int
sdp_mac_dbg_print_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdp_mac_dbg_print_show, inode->i_private);
}

static const struct file_operations sdp_mac_dbg_print_fops = {
	.open		= sdp_mac_dbg_print_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int
sdp_mac_dbg_regdump_get(void *data, u64 *val)
{
	SDP_GMAC_DEV_T *pGmacDev = data;

	sdp_mac_gmac_dump(pGmacDev);
	sdp_mac_dma_dump(pGmacDev);
	*val = 0x0;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mac_dbg_regdump_fops, sdp_mac_dbg_regdump_get, NULL,
	"%lld\n");

static int
sdp_mac_dbg_selftest_get(void *data, u64 *val)
{
	SDP_GMAC_DEV_T *pGmacDev = data;
	struct net_device *pNetDev = platform_get_drvdata(to_platform_device(pGmacDev->pDev));
	bool if_running = netif_running(pNetDev);
	int inter_err = 0, exter_err = 0;

	if( !pGmacDev->phydev ) {
		dev_err(pGmacDev->pDev, "Could not find PHY for selftest!\n");
		return -ENODEV;
	}

	if (if_running) {
		/* indicate we're in test mode */
		sdpGmac_netDev_close(pNetDev);
	}


	exter_err = sdp_mac_selftest_do(pNetDev, SDP_EXT_LB);

	if(exter_err <= 0) {
		//MAC loopback test
		inter_err = sdp_mac_selftest_do(pNetDev, SDP_MAC_LB);
	}

	//Reset GMAC H/W
	if(sdpGmac_reset(pNetDev) < 0){
		dev_err(pGmacDev->pDev, "GMAC H/W Reset error!\n");
	} else {
		//register initalize
		sdpGmac_dmaInit(pNetDev);
		sdpGmac_gmacInit(pNetDev);
		sdp_ring_set_index(pGmacDev, &pGmacDev->rx_ring, 0);
		sdp_ring_set_index(pGmacDev, &pGmacDev->tx_ring, 0);
		if(is_valid_ether_addr(pNetDev->dev_addr)) {
			sdpGmac_setMacAddr(pNetDev, (const u8*)pNetDev->dev_addr);
		}
	}
	pr_info("\n");
	dev_info(pGmacDev->pDev, "sdp_mac_dbg_selftest: selftest done.\n");
	dev_info(pGmacDev->pDev, "%s: %s(%d)\n", sdp_mac_ethtool_selftest_string[SDP_EXT_LB], exter_err>=0?"Pass":"Fail", exter_err);
	dev_info(pGmacDev->pDev, "%s: %s(%d)\n", sdp_mac_ethtool_selftest_string[SDP_MAC_LB], inter_err>=0?"Pass":"Fail", inter_err);

	if (if_running) {
		sdpGmac_netDev_open(pNetDev);
	}

	*val = ((u64)((u16)inter_err) << 32) | ((u16)exter_err);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mac_dbg_selftest_fops, sdp_mac_dbg_selftest_get, NULL,
	"0x%016llx\n");


static int
sdp_mac_dbg_mdio_get(void *data, u64 *val)
{
	SDP_GMAC_DEV_T *pGmacDev = data;

	sdp_mac_phy_regdump(pGmacDev, "sdp_mac_dbg_mdio");

	*val = 0x0;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mac_dbg_mdio_fops, sdp_mac_dbg_mdio_get, NULL,
	"0x%llx\n");


/* create debugfs node! */
static void
sdp_mac_add_debugfs(struct net_device *pNetDev)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);
	struct dentry *root;

	root = debugfs_create_dir(dev_name(pGmacDev->pDev), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	pGmacDev->debugfs_root = root;

	if (!debugfs_create_file("print", S_IRUSR, root, pGmacDev, &sdp_mac_dbg_print_fops))
		goto err_node;

	if (!debugfs_create_file("regdump", S_IRUSR, root, pGmacDev, &sdp_mac_dbg_regdump_fops))
		goto err_node;

	if (!debugfs_create_file("selftest", S_IRUSR, root, pGmacDev, &sdp_mac_dbg_selftest_fops))
		goto err_node;

	if (!debugfs_create_file("mdio", S_IRUSR, root, pGmacDev, &sdp_mac_dbg_mdio_fops))
		goto err_node;

	if(pNetDev->irq <= 0) {
		if(!debugfs_create_u32("polling_interval_us", S_IRUGO|S_IWUGO, root, &pGmacDev->polling_interval_us))
			goto err_node;
	}

	return;

err_node:
	debugfs_remove_recursive(root);
	pGmacDev->debugfs_root = NULL;
err_root:
	dev_err(pGmacDev->pDev, "failed to initialize debugfs\n");
}

static void
sdp_mac_remove_debugfs(struct net_device *pNetDev)
{
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);

	debugfs_remove_recursive(pGmacDev->debugfs_root);
}
#endif


static int sdpGmac_rtl8201_clkout_fixup(struct phy_device *phydev)
{
	const u32 RTL820x_TEST_REG = 0x19;
	u32 phyVal;

	dev_printk(KERN_DEBUG, &phydev->dev, "RTL8201x Phy RMII mode tx clock output fixup.\n");

	// Test regiser, Clock output set
	phyVal = (u16)phy_read(phydev, RTL820x_TEST_REG);

	phyVal = phyVal & ~(1UL << 11);/*RMII_CLKIN*/
	phyVal = phyVal & ~(1UL << 13);

	phy_write(phydev, RTL820x_TEST_REG, (u16)phyVal);

	return 0;
}

static int sdpGmac_probe(struct net_device *pNetDev)
{
	int retVal = 0;
//	const u8 defaultMacAddr[N_MAC_ADDR] = DEFAULT_MAC_ADDR;
	SDP_GMAC_DEV_T *pGmacDev = netdev_priv(pNetDev);


	struct mii_bus *sdp_mdiobus = NULL;
	struct sdp_mac_mdiobus_priv *mdiobus_priv = NULL;
	u32 len;

	struct phy_device *phydev = NULL;
	phy_interface_t phyinterface = sdp_mac_get_interface(pNetDev);
	int ret = 0;

	int phy_addr = 0;

	DPRINTK_GMAC ("called\n");

	spin_lock_init(&pGmacDev->lock);

	phy_register_fixup_for_uid(0x001CC810, 0x001ffff0, sdpGmac_rtl8201_clkout_fixup);

	//mdio bus register
	sdp_mdiobus = mdiobus_alloc();
	if(!sdp_mdiobus) {
		goto __probe_out_err;
	}

	sdp_mdiobus->name = "sdp_mdiobus";
	snprintf(sdp_mdiobus->id, MII_BUS_ID_SIZE, "%08x.mdio",
		(u32)(platform_get_resource(to_platform_device(pGmacDev->pDev), IORESOURCE_MEM, 0)->start));
	sdp_mdiobus->phy_mask = pGmacDev->plat->phy_mask/*all addr 1 is ignored */;
	sdp_mdiobus->irq = kzalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	for(retVal = 0; retVal < PHY_MAX_ADDR; retVal++) sdp_mdiobus->irq[retVal] = PHY_POLL;
	sdp_mdiobus->read = sdp_mac_mdio_read;
	sdp_mdiobus->write = sdp_mac_mdio_write;
	mdiobus_priv = kzalloc(sizeof(struct sdp_mac_mdiobus_priv), GFP_KERNEL);
	mdiobus_priv->addr = (u32)&pGmacDev->pGmacBase->gmiiAddr;
	mdiobus_priv->data = (u32)&pGmacDev->pGmacBase->gmiiData;
	mdiobus_priv->pGmacDev = pGmacDev;

	if(pGmacDev->plat->clk_csr <= 35000000)
		mdiobus_priv->cr = 2;
	else if(pGmacDev->plat->clk_csr <= 60000000)
		mdiobus_priv->cr = 3;
	else if(pGmacDev->plat->clk_csr <= 100000000)
		mdiobus_priv->cr = 0;
	else if(pGmacDev->plat->clk_csr <= 150000000)
		mdiobus_priv->cr = 1;
	else if(pGmacDev->plat->clk_csr <= 250000000)
		mdiobus_priv->cr = 4;
	else
		mdiobus_priv->cr = 5;

	sdp_mdiobus->priv = mdiobus_priv;

	/* if PHY 0x6 is RTL8304E */
	if(sdp_mdiobus->read(sdp_mdiobus, 0x6, 0x2) == 0x001C && sdp_mdiobus->read(sdp_mdiobus, 0x6, 0x3) == 0xC852) {
		/* RTL82xxx addr 0x0 broadcast disable */
		dev_info(pGmacDev->pDev, "RTL82xxx addr 0x0 broadcast disable\n");
		sdp_mdiobus->write(sdp_mdiobus, 0x0, 31, 0x7);
		sdp_mdiobus->write(sdp_mdiobus, 0x0, 20, 0x10d5);
		sdp_mdiobus->write(sdp_mdiobus, 0x0, 31, 0x0);
	}


	retVal = mdiobus_register(sdp_mdiobus);
	if(retVal < 0) {
		mdiobus_free(sdp_mdiobus);
		dev_err(pGmacDev->pDev, "mdiobus_register error %d\n", retVal);
		goto __probe_out_err;
	}
	pGmacDev->mdiobus = sdp_mdiobus;
	pGmacDev->phydev = NULL;
	pGmacDev->oldlink = 0;

	//init Phy. use phy subsystem.
	dev_dbg(pGmacDev->pDev, "Init Phy!!\n");

	phyinterface = sdp_mac_get_interface(pNetDev);

	/* check RTL8304E */
	phy_addr = 0x6;
	if (sdp_mdiobus->phy_map[phy_addr] && sdp_mdiobus->phy_map[phy_addr]->phy_id == 0x001CC852) {
		dev_info(pGmacDev->pDev, "find RTL8304E Switch at address %d. (id:0x%04x)\n",
			sdp_mdiobus->phy_map[phy_addr]->addr, sdp_mdiobus->phy_map[phy_addr]->phy_id);
			phydev = sdp_mdiobus->phy_map[phy_addr];
	} else {
		for(phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
			if (sdp_mdiobus->phy_map[phy_addr]) {
				/*
				 * Broken HW is sometimes missing the pull-up resistor on the
				 * MDIO line, which results in reads to non-existent devices returning
				 * 0 rather than 0xffff. Catch this here and treat 0 as a non-existent
				 * device as well.
				 * Note: phydev->phy_id is the result of reading the UID PHY registers.
				 */
				if(sdp_mdiobus->phy_map[phy_addr]->phy_id) {
					/* this is first valied phydev */
					dev_info(pGmacDev->pDev, "find PHY at address %d. (id:0x%04x)\n",
						sdp_mdiobus->phy_map[phy_addr]->addr, sdp_mdiobus->phy_map[phy_addr]->phy_id);
					phydev = sdp_mdiobus->phy_map[phy_addr];
					break;
				}
			}
		}
	}

	if (IS_ERR_OR_NULL(phydev)) {
		if(pGmacDev->plat->fixed_phy_added) {
			/* fixed phy */
			char bus_id[MII_BUS_ID_SIZE + 3];
			dev_info(pGmacDev->pDev, "try to use fixed PHY("PHY_ID_FMT")...\n", "fixed-0", pGmacDev->plat->fixed_phy_id);

			sprintf(bus_id, PHY_ID_FMT, "fixed-0", pGmacDev->plat->fixed_phy_id);
			phydev = phy_connect(pNetDev, bus_id, sdp_mac_adjust_link, pGmacDev->plat->fixed_rgmii?PHY_INTERFACE_MODE_RGMII:PHY_INTERFACE_MODE_RMII);
			if(IS_ERR_OR_NULL(phydev)) {
				dev_err(pGmacDev->pDev, "Could not connect to Fixed PHY("PHY_ID_FMT")!!!\n",
						"fixed-0", pGmacDev->plat->fixed_phy_id);
				return PTR_ERR(phydev);
			}
		} else {
			dev_err(pGmacDev->pDev, "Could not find PHY!!!\n");
			return -ENODEV;
		}
	} else {
		/* real phy */
		ret = phy_connect_direct(pNetDev, phydev, sdp_mac_adjust_link, phyinterface);
		if (ret < 0) {
			dev_err(pGmacDev->pDev, "Could not connect to PHY!!!\n");
			return ret;
		}
	}

	pGmacDev->phydev = phydev;

	phy_start(phydev);
	dev_dbg(pGmacDev->pDev, "attached to PHY (UID 0x%x)"
		 " Link = %d\n", phydev->phy_id, phydev->link);


	if(sdpGmac_pad_hw_reset(pGmacDev) < 0) {
		dev_err(pGmacDev->pDev, "Pad H/W Reset Error!!!\n");
		retVal = -ENODEV;		// 0.952
		goto  __probe_out_err;		// 0.952
	}

	/* Device reset MAC addr is cleared*/
	if(sdpGmac_reset(pNetDev) < 0){
		dev_err(pGmacDev->pDev, "S/W Reset Error!!!\n");
		retVal = -ENODEV;		// 0.952
		goto  __probe_out_err;		// 0.952
	}

	ether_setup(pNetDev);

	/* init napi struct */
	netif_napi_add(pNetDev, &pGmacDev->napi, sdpGmac_netDev_poll, pGmacDev->plat->napi_weight);
	dev_dbg(pGmacDev->pDev, "initialized a napi context. weight=%d\n", pGmacDev->napi.weight);

	pNetDev->ethtool_ops = &sdpGmac_ethtool_ops;
	pNetDev->netdev_ops = &sdpGmac_netdev_ops;
	pNetDev->watchdog_timeo = 5 * HZ;


	len = ETH_DATA_LEN + ETHER_PACKET_EXTRA + 4;
	len += pGmacDev->bus_align - 1;
	len &= pGmacDev->bus_mask;

// net stats init
	pGmacDev->netStats.rx_errors 	    = 0;
	pGmacDev->netStats.collisions       = 0;
	pGmacDev->netStats.rx_crc_errors    = 0;
	pGmacDev->netStats.rx_frame_errors  = 0;
	pGmacDev->netStats.rx_length_errors = 0;

// TODO check RING MODE and CHAIN MODE
	pGmacDev->pTxDesc = dma_alloc_coherent(pGmacDev->pDev, TX_DESC_SIZE,
						&pGmacDev->txDescDma, GFP_ATOMIC);
	if(pGmacDev->pTxDesc == NULL) {
		retVal = -ENOMEM;
		goto __probe_out_err;
	}

	pGmacDev->pRxDesc = dma_alloc_coherent(pGmacDev->pDev, RX_DESC_SIZE,
						&pGmacDev->rxDescDma, GFP_ATOMIC);

	if(pGmacDev->pRxDesc == NULL) {
		retVal = -ENOMEM;
		goto __probe_out_err2;
	}

	sdp_mac_ring_init(pGmacDev);

	if(retVal < 0) {
		retVal = -EPERM;
		goto __probe_out_err3;
	}

// register initalize
	sdpGmac_dmaInit(pNetDev);
	sdpGmac_gmacInit(pNetDev);

	if(pNetDev->irq > 0) {
		// request interrupt resource
		retVal = request_irq(pNetDev->irq, sdpGmac_intrHandler,
			0, dev_name(pGmacDev->pDev), pNetDev);
		if(retVal < 0) goto __probe_out_err3;

		disable_irq(pNetDev->irq);
	} else {
		/* polling.. set timer */
		pGmacDev->polling_interval_us = 4000;
		hrtimer_init(&pGmacDev->polling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		pGmacDev->polling_timer.function = sdp_mac_polling_callback;
		dev_err(pGmacDev->pDev, "start polling timer! interval: %dus\n", pGmacDev->polling_interval_us);
	}

	if(register_netdev(pNetDev) < 0) {
		dev_err(pGmacDev->pDev, "register netdev error\n");
		retVal = -EPERM;
		goto __probe_out_err4;
	}

	retVal = sdp_mac_get_interface(pNetDev);
	dev_info(pGmacDev->pDev, "Clk %luMHz, IRQ is %d%s, use %s mode, %dbit BUS\n", pGmacDev->plat->clk_csr/1000000, pNetDev->irq,
		pNetDev->irq?"":"(polling)", retVal==PHY_INTERFACE_MODE_RGMII?"RGMII":(retVal==PHY_INTERFACE_MODE_RMII?"RMII":"NA!!"),
		pGmacDev->plat->bus_width);
	dev_info(pGmacDev->pDev, "desc ring size is rx%lu/tx%lu * %dbytes\n",
		pGmacDev->rx_ring.size, pGmacDev->tx_ring.size, sizeof(DMA_DESC_T));


#if defined(CONFIG_MAC_SET_BY_USER)
	pNetDev->dev_addr[0] = (u8)((CONFIG_MAC_FRONT_4BYTE >> 24) & 0xFF);
	pNetDev->dev_addr[1] = (u8)((CONFIG_MAC_FRONT_4BYTE >> 16) & 0xFF);
	pNetDev->dev_addr[2] = (u8)((CONFIG_MAC_FRONT_4BYTE >> 8) & 0xFF);
	pNetDev->dev_addr[3] = (u8)((CONFIG_MAC_FRONT_4BYTE) & 0xFF);
	pNetDev->dev_addr[4] = (u8)((CONFIG_MAC_END_2BYTE >> 8) & 0xFF);
	pNetDev->dev_addr[5] = (u8)((CONFIG_MAC_END_2BYTE) & 0xFF);

#elif defined(CONFIG_MAC_SET_RAMDOM)
	dev_info(pGmacDev->pDev, "generate software assigned random ethernet addr\n");
	eth_hw_addr_random(pNetDev);
#endif


	if(!is_valid_ether_addr(pNetDev->dev_addr)){
		dev_warn(pGmacDev->pDev, "Invalid ethernet MAC address. Please "
				"set using ifconfig\n");
	}
	else {
		dev_info(pGmacDev->pDev, "Ethernet address %02x:%02x:%02x:%02x:%02x:%02x\n",
		     *pNetDev->dev_addr, *(pNetDev->dev_addr+1),
		     *(pNetDev->dev_addr+2), *(pNetDev->dev_addr+3),
		     *(pNetDev->dev_addr+4), *(pNetDev->dev_addr+5));

		sdpGmac_setMacAddr(pNetDev, (const u8*)pNetDev->dev_addr);
	}

	DPRINTK_GMAC ("exit\n");
	return 0;

	unregister_netdev(pNetDev);
__probe_out_err4:
	free_irq(pNetDev->irq, pNetDev);
__probe_out_err3:
	dma_free_coherent(pGmacDev->pDev, RX_DESC_SIZE,
				pGmacDev->pRxDesc, pGmacDev->rxDescDma);
__probe_out_err2:
	dma_free_coherent(pGmacDev->pDev, TX_DESC_SIZE,
				pGmacDev->pTxDesc, pGmacDev->txDescDma);
__probe_out_err:
	return retVal;
}


static inline int sdpGmac_setMemBase(int id, struct resource *pRes, SDP_GMAC_DEV_T* pGmacDev)
{
	int	retVal = 0;
	void  	*remapAddr;
	size_t 	size = (size_t)(pRes->end-pRes->start);


// TODO: request_mem_region

	if (id < N_REG_BASE) {
		remapAddr = (void*)ioremap_nocache((size_t)pRes->start, size);
	} else {
		DPRINTK_GMAC_ERROR("id is wrong \n");
		return retVal -1;
	}


	switch(id){
		case (0):
		   pGmacDev->pGmacBase = (SDP_GMAC_T*)remapAddr;
		   DPRINTK_GMAC ("GMAC remap is 0x%08x\n",(int) remapAddr);
		   break;

		case (1):
		   pGmacDev->pMmcBase = (SDP_GMAC_MMC_T*)remapAddr;
		   DPRINTK_GMAC ("Gmac manage count remap is 0x%08x\n",(int) remapAddr);
		   break;

		case (2):
		   pGmacDev->pTimeStampBase = (SDP_GMAC_TIME_STAMP_T*)remapAddr;
		   DPRINTK_GMAC ("time stamp remap is 0x%08x\n",(int) remapAddr);
		   break;

		case (3):
		   pGmacDev->pMac2ndBlk = (SDP_GMAC_MAC_2ND_BLOCK_T*)remapAddr;
		   DPRINTK_GMAC ("mac 2nd remap is 0x%08x\n",(int) remapAddr);
		   break;

		case (4):
		   pGmacDev->pDmaBase = (SDP_GMAC_DMA_T*)remapAddr;
		   DPRINTK_GMAC ("DMA remap is 0x%08x\n",(int) remapAddr);
		   break;

		default:
		   break;
	}

//	DPRINTK_GMAC ("exit\n");

	return retVal;
}

#ifdef CONFIG_OF

static u64 sdp_mac_dmamask = DMA_BIT_MASK(32);

static int sdp_mac_parse_dt(struct device *dev, struct sdp_gmac_plat *plat)
{
	const char *clkname;
	struct clk *clk = ERR_PTR(-EINVAL);

	if(!dev->of_node)
	{
		dev_err(dev, "device tree node not found\n");
		return -EINVAL;
	}

	if(of_property_read_u32(dev->of_node, "bus_width", &plat->bus_width))
	{
		dev_info(dev, "bus_width property not found, using default 32bit\n");
		plat->bus_width = 32;
	}

	if(of_property_read_u32(dev->of_node, "phy_mask", &plat->phy_mask))
	{
		dev_info(dev, "phy_mask property not found, using default value\n");
		plat->phy_mask = 0;
	}

	if(of_property_read_u32(dev->of_node, "napi_weight", &plat->napi_weight))
	{
		dev_info(dev, "napi_weight property not found, using default value\n");
		plat->napi_weight = 128;
	}

	if(of_property_read_string(dev->of_node, "clock-names", &clkname))
	{
		dev_info(dev, "clock-names property not found, using default value\n");
		clkname = "emac_clk";
	}

	clk = clk_get(dev, clkname);
	if (IS_ERR(clk))
	{
		plat->clk_csr = 166000000;
		dev_err(dev, "cannot get %s, set default clock rate %luMHz\n", clkname, plat->clk_csr/1000000);
	}
	else
	{
		plat->clk_csr = clk_get_rate(clk);
	}

#ifdef CONFIG_FIXED_PHY
	{
		struct device_node *fixed_link;

		fixed_link = of_get_child_by_name(dev->of_node, "fixed-link");
		if (fixed_link) {
			of_property_read_u32(fixed_link, "phy-id", &plat->fixed_phy_id);
			plat->fixed_rgmii = of_property_read_bool(fixed_link, "rgmii");
			plat->fixed_phy_added = true;
		}
	}
#endif

	{
		const u32 *u32value = NULL;
		u32 size = 0;
		u32 onesize = 3;
		int i, j, item_num;
		const char *prop_name[] = {"phy_sel_reg", "pad_ctrl_reg"};
		struct sdp_mac_reg_list *table_addr[2] = {NULL};

		table_addr[0] = &plat->select_rgmii;
		table_addr[1] = &plat->padctrl;

		item_num = 2;

		/* Get propertys */
		for(j = 0; j < item_num; j++) {
			u32value = of_get_property(dev->of_node, prop_name[j], &size);
			if(u32value) {
				size /= sizeof(u32);

				table_addr[j]->list_num = (int)size / 3;
				table_addr[j]->list = kmalloc(size * sizeof(struct sdp_mac_reg_set), GFP_KERNEL);

				for(i = 0; size >= onesize; size -= onesize, u32value += onesize, i++) {
					table_addr[j]->list[i].addr = be32_to_cpu(u32value[0]);
					table_addr[j]->list[i].mask = be32_to_cpu(u32value[1]);
					table_addr[j]->list[i].value = be32_to_cpu(u32value[2]);
				}
			}
		}

	#if 0/* debug, print table */
		for(j = 0; j < item_num; j++) {
			for(i = 0; i < table_addr[j]->list_num; i++) {
				dev_printk(KERN_INFO, dev,
					"%s%2d addr 0x%08llx, mask 0x%08llx, val 0x%08x\n",
					prop_name[j], i, (u64)table_addr[j]->list[i].addr, (u64)table_addr[j]->list[i].mask, table_addr[j]->list[i].value);
			}
		}
	#endif
	}

#ifdef CONFIG_ARM_LPAE
	dev->coherent_dma_mask = DMA_BIT_MASK(64);
#else
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
#endif
	dev->dma_mask = &sdp_mac_dmamask;
	
	return 0;
}

#endif


/* Linux probe and remove */
static int sdpGmac_drv_probe (struct platform_device *pDev)
{
	struct resource *pRes = pDev->resource;
	struct net_device *pNetDev;
	SDP_GMAC_DEV_T *pGmacDev;
	int retVal = 0;
	int i = 0;

	DPRINTK_GMAC_FLOW ("called\n");


	if(pRes == NULL) goto __probe_drv_out;

// net device
	pNetDev = alloc_etherdev(sizeof(SDP_GMAC_DEV_T));

    if (!pNetDev) {
        dev_err(&pDev->dev, "could not allocate device.\n");
        retVal = -ENOMEM;
        goto __probe_drv_out;
    }
	SET_NETDEV_DEV(pNetDev, &pDev->dev);

	pNetDev->dma = (unsigned char) -1;
	pGmacDev = netdev_priv(pNetDev);
#ifdef CONFIG_ARCH_SDP1004
	pGmacDev->revision = sdp_get_revision_id();
	dev_info(&pDev->dev, "Firenze revision %d\n", pGmacDev->revision);
#endif
	pGmacDev->pNetDev = pNetDev;
// need to request dma memory
	pGmacDev->pDev = &pDev->dev;

	pGmacDev->pGmacBase = NULL;
	pGmacDev->pMmcBase = NULL;
	pGmacDev->pTimeStampBase = NULL;
	pGmacDev->pMac2ndBlk = NULL;
	pGmacDev->pDmaBase = NULL;

#ifdef CONFIG_OF
	pDev->dev.platform_data = kzalloc(sizeof(struct sdp_gmac_plat), GFP_KERNEL);
	if(!pDev->dev.platform_data) {
		free_netdev(pNetDev);
		dev_err(&pDev->dev, "can not alloc plat data!!!\n");
		return -ENOMEM;
	}

	if(sdp_mac_parse_dt(&pDev->dev, pDev->dev.platform_data) < 0)
	{
		kfree(pDev->dev.platform_data);
		free_netdev(pNetDev);
		dev_err(&pDev->dev, "Error in parsing device tree!!!\n");
		return -EINVAL;
	}
#endif

	/* set platform data! */
	pGmacDev->plat = dev_get_platdata(&pDev->dev);
	if(!pGmacDev->plat) {
		free_netdev(pNetDev);
		dev_err(&pDev->dev, "platform data is not exist!!!\n");
		return -EINVAL;
	}

	if((pGmacDev->plat->bus_width % 8) > 0) {
		free_netdev(pNetDev);
		dev_err(&pDev->dev, "invalied bus width(%d)!!\n", pGmacDev->plat->bus_width);
		return -EINVAL;
	}
	pGmacDev->bus_align = pGmacDev->plat->bus_width / 8;
	pGmacDev->bus_mask = ~(pGmacDev->bus_align-1);

// GMAC resource initialize
	for (i = 0; i < N_REG_BASE; i++) {
		pRes = platform_get_resource(pDev, IORESOURCE_MEM, (u32)i);
		if(!pRes){
			dev_err(&pDev->dev, "could not find device %d resource.\n", i);
			retVal = -ENODEV;
			goto __probe_drv_err;
		}
		sdpGmac_setMemBase(i, pRes, pGmacDev);
	}

	retVal = platform_get_irq(pDev, 0);
	if(retVal < 0) {
		/* polling mode */\
		dev_info(&pDev->dev, "can't get irq. start polling mode!!\n");
		pNetDev->irq = 0;
	} else {
		pNetDev->irq = (u32)retVal;
	}

#ifdef CONFIG_ARCH_SDP1412
	if(pNetDev->irq > 0 && sdp_get_revision_id() < 1) {
		dev_info(&pDev->dev, "fixup: SoC is Hawk-A Rev0!! forced polling mode\n");
		pNetDev->irq = 0;
	}
#endif

// save resource about net driver
	platform_set_drvdata(pDev, pNetDev);

	retVal = sdpGmac_probe(pNetDev);

#ifdef CONFIG_DEBUG_FS
	sdp_mac_add_debugfs(pNetDev);
#endif

__probe_drv_err:
	if (retVal < 0) {
		if(pGmacDev->pGmacBase) iounmap(pGmacDev->pGmacBase);
		if(pGmacDev->pMmcBase) iounmap(pGmacDev->pMmcBase);
		if(pGmacDev->pTimeStampBase) iounmap(pGmacDev->pTimeStampBase);
		if(pGmacDev->pMac2ndBlk) iounmap(pGmacDev->pMac2ndBlk);
		if(pGmacDev->pDmaBase) iounmap(pGmacDev->pDmaBase);
		free_netdev(pNetDev);
	}

__probe_drv_out:
	DPRINTK_GMAC_FLOW ("exit\n");
	return retVal;
}


static int sdpGmac_drv_remove (struct platform_device *pDev)
{
	struct net_device *pNetDev;
	SDP_GMAC_DEV_T *pGmacDev;
	SDP_GMAC_T *pGmacReg;
	SDP_GMAC_DMA_T *pDmaReg;
	struct sdp_gmac_plat *plat = dev_get_platdata(&pDev->dev);

	DPRINTK_GMAC_FLOW ("called\n");

	if(!pDev) return 0;
	pNetDev = platform_get_drvdata(pDev);

	if(!pNetDev) return 0;
	pGmacDev = netdev_priv(pNetDev);

#ifdef CONFIG_DEBUG_FS
	sdp_mac_remove_debugfs(pNetDev);
#endif

	unregister_netdev(pNetDev);

	pGmacReg = pGmacDev->pGmacBase;
	pDmaReg = pGmacDev->pDmaBase;

	netif_stop_queue(pNetDev);
	netif_carrier_off(pNetDev);

	// rx, tx disable
	pDmaReg->operationMode &= ~(B_TX_EN | B_RX_EN) ;
	pGmacReg->configuration &= ~(B_TX_ENABLE | B_RX_ENABLE);

	// all interrupt disable
	pDmaReg->intrEnable = 0;
	pDmaReg->status = pDmaReg->status;	// Clear interrupt pending register

	// phy control

	// skb control
	sdp_ring_clear(pGmacDev, &pGmacDev->rx_ring);
	sdp_ring_clear(pGmacDev, &pGmacDev->tx_ring);

	// phy control
	if (pNetDev->phydev) {
		phy_stop(pNetDev->phydev);
		dev_dbg(pGmacDev->pDev, "disconnect to PHY (UID 0x%x)"
			 " Link = %d\n", pNetDev->phydev->phy_id, pNetDev->phydev->link);
		phy_disconnect(pNetDev->phydev);
	}

	pNetDev->phydev = NULL;
	pGmacDev->phydev = NULL;

	dma_free_coherent(pGmacDev->pDev, TX_DESC_SIZE,
				pGmacDev->pTxDesc, pGmacDev->txDescDma);

	dma_free_coherent(pGmacDev->pDev, RX_DESC_SIZE,
				pGmacDev->pRxDesc, pGmacDev->rxDescDma);

	free_irq(pNetDev->irq, pNetDev);
	iounmap(pGmacDev->pGmacBase);
	iounmap(pGmacDev->pTimeStampBase);
	iounmap(pGmacDev->pMac2ndBlk);
	iounmap(pGmacDev->pDmaBase);

	free_netdev(pNetDev);
	platform_set_drvdata(pDev, NULL);

#ifdef CONFIG_OF
	kfree(plat->select_rgmii.list);
	kfree(plat->padctrl.list);
	kfree(plat);
#endif

	DPRINTK_GMAC_FLOW ("exit\n");

	return 0;
}

//static struct platform_device *gpSdpGmacPlatDev;

static const struct of_device_id sdp_mac_dt_match[] = {
	{ .compatible = "samsung,sdp-mac" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_mac_dt_match);

#ifdef CONFIG_PM
static const struct dev_pm_ops sdpGmac_pm_ops = {
	.suspend_late  = sdpGmac_drv_suspend,
	.resume_early  = sdpGmac_drv_resume,
};
#endif

static struct platform_driver sdpGmac_device_driver = {
	.probe		= sdpGmac_drv_probe,
	.remove		= sdpGmac_drv_remove,
	.driver		= {
		.name	= ETHER_NAME,
		.owner  = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sdp_mac_dt_match,
#endif

#ifdef CONFIG_PM
		.pm = &sdpGmac_pm_ops,
#endif
	},
};

/* Module init and exit */
static int __init sdpGmac_init(void)
{
	int retVal = 0;

	DPRINTK_GMAC_FLOW ("called\n");
	pr_info("%s: registered SDP GMAC network driver. ver %s\n",
		sdpGmac_device_driver.driver.name, GMAC_DRV_VERSION);

	retVal = platform_driver_register(&sdpGmac_device_driver);
	if(retVal) retVal = -ENODEV;

	DPRINTK_GMAC_FLOW ("exit\n");
	return retVal;
}


static void __exit sdpGmac_exit(void)
{
	DPRINTK_GMAC_FLOW ("called\n");

	platform_driver_unregister(&sdpGmac_device_driver);
//	platform_device_unregister(gpSdpGmacPlatDev);

	DPRINTK_GMAC_FLOW ("exit\n");
}


module_init(sdpGmac_init);
module_exit(sdpGmac_exit);

MODULE_AUTHOR("VD Division, Samsung Electronics Co.");
MODULE_LICENSE("GPL");

