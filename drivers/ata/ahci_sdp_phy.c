#include <linux/list.h>
#include <linux/module.h>
#include <linux/ahci_sdp.h>
#include <linux/platform_device.h>

/* common */
int sdp_sata_phy_init(struct sdp_sata_phy *phy)
{
	if (phy && phy->ops.init)
		return phy->ops.init(phy);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(sdp_sata_phy_init);

int sdp_sata_phy_exit(struct sdp_sata_phy *phy)
{
	if (phy && phy->ops.exit)
		return phy->ops.exit(phy);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(sdp_sata_phy_exit);

static LIST_HEAD(sdp_phy_list);
static DEFINE_SPINLOCK(sdp_phy_lock);

static void devm_sdp_sata_phy_release(struct device *dev, void *res)
{
	struct sdp_sata_phy *phy = *(struct sdp_sata_phy**)res;
	put_device(phy->dev);
	module_put(phy->dev->driver->owner);
}

struct sdp_sata_phy* devm_sdp_sata_phy_get_by_node(struct device *dev, struct device_node *node)
{
	struct sdp_sata_phy *phy, *ret = NULL;
	unsigned long flags;

	if (!node)
		goto end_find;

	spin_lock_irqsave(&sdp_phy_lock, flags);
	list_for_each_entry(phy, &sdp_phy_list, link) {
		if (node == phy->dev->of_node) {
			ret = phy;
			break;
		}
	}
	spin_unlock_irqrestore(&sdp_phy_lock, flags);

	if (ret) {
		struct sdp_sata_phy **devres = devres_alloc(devm_sdp_sata_phy_release,
			sizeof(*devres), GFP_KERNEL);
		if (!devres) {
			ret = ERR_PTR(-ENOMEM);
			goto end_find;
		};
		if (!try_module_get(ret->dev->driver->owner)) {
			ret = ERR_PTR(-ENODEV);
			devres_free(devres);
			goto end_find;
		}
		*devres = ret;
		devres_add(dev, devres);
		get_device(phy->dev);
	} else {
		ret = ERR_PTR(-ENODEV);
	}

end_find:
	return ret;
}
EXPORT_SYMBOL(devm_sdp_sata_phy_get_by_node);

int sdp_sata_phy_register(struct sdp_sata_phy *phy)
{
	unsigned long flags;

	/* no colision check */
	spin_lock_irqsave(&sdp_phy_lock, flags);
	list_add_tail(&phy->link, &sdp_phy_list);
	spin_unlock_irqrestore(&sdp_phy_lock, flags);

	dev_info(phy->dev, "registered.\n");
	return 0;
}
EXPORT_SYMBOL(sdp_sata_phy_register);

int sdp_sata_phy_unregister(struct sdp_sata_phy *phy)
{
	unsigned long flags;

	/* no colision check */
	spin_lock_irqsave(&sdp_phy_lock, flags);
	list_del(&phy->link);
	spin_unlock_irqrestore(&sdp_phy_lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_sata_phy_unregister);

MODULE_ALIAS("platform: sdp_ahci_phy");
MODULE_AUTHOR("ij.jang@samsung.com");
MODULE_DESCRIPTION("SDP SATA phy common driver");
MODULE_LICENSE("GPL");
