/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Streams of an IOMMU that a hypervisor owns on behalf of the kernel: the
 * kernel cannot program them, and may only have memory it gave up to the
 * secure world (protected memory) mapped into them.
 */
#ifndef __LINUX_IOMMU_PROT_H
#define __LINUX_IOMMU_PROT_H

#include <linux/errno.h>
#include <linux/types.h>

struct device;

/**
 * struct iommu_prot_ops - a provider of protected streams
 * @may_probe: whether the kernel's driver of the IOMMU at physical address
 *	@base may probe now. Not while the hypervisor is taking the IOMMU
 *	over: the driver would program the streams and contexts it keeps.
 * @declare: name stream @id with @mask of the IOMMU at @base as one the
 *	provider owns. Only possible at boot, before the hypervisor isolates
 *	the IOMMU from the kernel: the kernel cannot add a stream later.
 * @can_protect: whether the provider owns stream @id with @mask of the
 *	IOMMU at @base (it was declared and accepted)
 * @map: map @size bytes of protected memory at @paddr to @iova for the
 *	stream, with @prot (IOMMU_READ, IOMMU_WRITE). Memory that is not
 *	protected, or already mapped into a protected stream, is refused. It
 *	cannot be returned to the kernel until it is unmapped. A failure
 *	leaves nothing mapped.
 * @unmap: undo exactly one @map
 *
 * @id and @mask are in the format of the IOMMU's firmware description
 * (for an Arm SMMU: the stream ID and the SMR mask).
 */
struct iommu_prot_ops {
	bool (*may_probe)(phys_addr_t base);
	int (*declare)(phys_addr_t base, u32 id, u32 mask);
	bool (*can_protect)(phys_addr_t base, u32 id, u32 mask);
	int (*map)(phys_addr_t base, u32 id, u32 mask, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	int (*unmap)(phys_addr_t base, u32 id, u32 mask, unsigned long iova,
		     size_t size);
};

#ifdef CONFIG_IOMMU_PROT
int iommu_prot_register(const struct iommu_prot_ops *ops);
const struct iommu_prot_ops *iommu_prot_get_ops(void);
int iommu_prot_declare(phys_addr_t base, u32 id, u32 mask);
bool iommu_prot_dma_allowed(struct device *dev);
bool iommu_prot_device(struct device *dev);
#else
static inline int iommu_prot_register(const struct iommu_prot_ops *ops)
{
	return -ENODEV;
}

static inline int iommu_prot_declare(phys_addr_t base, u32 id, u32 mask)
{
	return -ENODEV;
}

static inline bool iommu_prot_dma_allowed(struct device *dev)
{
	return true;
}

static inline bool iommu_prot_device(struct device *dev)
{
	return false;
}

static inline const struct iommu_prot_ops *iommu_prot_get_ops(void)
{
	return NULL;
}
#endif

#endif /* __LINUX_IOMMU_PROT_H */
