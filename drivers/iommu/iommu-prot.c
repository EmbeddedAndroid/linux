// SPDX-License-Identifier: GPL-2.0-only
/*
 * Provider of IOMMU streams owned by a hypervisor (pKVM, Xen): the one
 * registered at boot, before the streams are declared and before the
 * IOMMUs probe.
 */

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/iommu.h>
#include <linux/iommu-prot.h>

static const struct iommu_prot_ops *iommu_prot_provider;

int iommu_prot_register(const struct iommu_prot_ops *ops)
{
	if (cmpxchg(&iommu_prot_provider, NULL, ops))
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL_GPL(iommu_prot_register);

const struct iommu_prot_ops *iommu_prot_get_ops(void)
{
	return READ_ONCE(iommu_prot_provider);
}
EXPORT_SYMBOL_GPL(iommu_prot_get_ops);

/*
 * For the code that knows which streams must be protected: at boot, before
 * device_initcall, when hypervisors still take declarations.
 */
int __init iommu_prot_declare(phys_addr_t base, u32 id, u32 mask)
{
	const struct iommu_prot_ops *ops = iommu_prot_get_ops();

	if (!ops || !ops->declare)
		return -ENODEV;
	return ops->declare(base, id, mask);
}

/**
 * iommu_prot_dma_allowed() - whether @dev may be given protected memory
 * @dev: the device importing it
 *
 * When a hypervisor protects memory by owning the streams that may reach
 * it, a device the kernel's IOMMU translates through any other stream is
 * refused that memory: where the hypervisor isolates the IOMMU its DMA
 * would only fault, and some devices hang on such faults; where it could
 * not, the device's DMA would reach that memory.
 * Devices without an IOMMU, such as the TEE, and every device on systems
 * without a provider are allowed.
 */
bool iommu_prot_dma_allowed(struct device *dev)
{
	if (!iommu_prot_get_ops() || !device_iommu_mapped(dev))
		return true;
	return dev->iommu && dev->iommu->prot_stream;
}
EXPORT_SYMBOL_GPL(iommu_prot_dma_allowed);

/**
 * iommu_prot_device() - whether @dev is translated through a stream that
 * a provider owns, so that only protected memory can be mapped for it
 * @dev: the device
 */
bool iommu_prot_device(struct device *dev)
{
	return iommu_prot_get_ops() && device_iommu_mapped(dev) &&
	       dev->iommu && dev->iommu->prot_stream;
}
EXPORT_SYMBOL_GPL(iommu_prot_device);
