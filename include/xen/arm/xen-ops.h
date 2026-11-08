/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_OPS_H
#define _ASM_ARM_XEN_OPS_H

#include <linux/iommu-dma.h>
#include <xen/swiotlb-xen.h>
#include <xen/xen-ops.h>

static inline void xen_setup_dma_ops(struct device *dev)
{
#ifdef CONFIG_XEN
	if (!xen_swiotlb_detect())
		return;

	/*
	 * A device whose DMA goes through an IOMMU the domain drives itself
	 * is translated by that IOMMU and never needs Xen's bounce buffers.
	 * The IOMMU core can run this before it sets up the default domain,
	 * so drop the ops again once the device has turned out to use it.
	 */
	if (!use_dma_iommu(dev))
		dev->dma_ops = &xen_swiotlb_dma_ops;
	else if (dev->dma_ops == &xen_swiotlb_dma_ops)
		dev->dma_ops = NULL;
#endif
}

#endif /* _ASM_ARM_XEN_OPS_H */
