// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-mapping.h>
#include "qda_drv.h"
#include "qda_gem.h"
#include "qda_memory_dma.h"
#include "qda_memory_manager.h"
#include "qda_soc.h"

/*
 * The DSP identifies the context bank a buffer belongs to from the upper bits
 * of its address, so the stream ID is folded into every address handed to it.
 * The DMA API only ever sees the plain address, which is why the context bank
 * devices are created with a mask that covers just those lower bits.
 */
static dma_addr_t qda_gem_dma_addr(struct qda_gem_obj *gem_obj)
{
	return qda_soc_dma_addr(gem_obj->iommu_dev->qdev->soc_data, gem_obj->dma_addr);
}

/**
 * qda_dma_alloc() - Allocate a buffer from a context bank
 * @iommu_dev: Context bank to allocate from
 * @gem_obj: GEM object to back with the allocation
 * @size: Size to allocate in bytes
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_dma_alloc(struct qda_iommu_device *iommu_dev,
		  struct qda_gem_obj *gem_obj, size_t size)
{
	dma_addr_t dma_addr;
	void *virt;

	virt = dma_alloc_coherent(iommu_dev->dev, size, &dma_addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	gem_obj->virt = virt;
	gem_obj->dma_addr = dma_addr +
			    qda_soc_sid_offset(iommu_dev->qdev->soc_data, iommu_dev->sid);
	gem_obj->iommu_dev = iommu_dev;
	qda_memory_manager_get_device(iommu_dev);

	return 0;
}

/**
 * qda_dma_free() - Release a buffer back to its context bank
 * @gem_obj: GEM object to free
 */
void qda_dma_free(struct qda_gem_obj *gem_obj)
{
	struct qda_iommu_device *iommu_dev = gem_obj->iommu_dev;

	dma_free_coherent(iommu_dev->dev, gem_obj->size,
			  gem_obj->virt, qda_gem_dma_addr(gem_obj));

	gem_obj->virt = NULL;
	gem_obj->dma_addr = 0;
	gem_obj->iommu_dev = NULL;
	qda_memory_manager_put_device(iommu_dev);
}

/**
 * qda_dma_mmap() - Map a buffer into user space
 * @gem_obj: GEM object to map
 * @vma: Virtual memory area to map into
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_dma_mmap(struct qda_gem_obj *gem_obj, struct vm_area_struct *vma)
{
	return dma_mmap_coherent(gem_obj->iommu_dev->dev, vma, gem_obj->virt,
				 qda_gem_dma_addr(gem_obj), gem_obj->size);
}

/**
 * qda_dma_get_sgtable() - Describe a buffer's pages for an importer
 * @gem_obj: GEM object to describe
 * @sgt: Table to fill in; the caller frees it with sg_free_table()
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_dma_get_sgtable(struct qda_gem_obj *gem_obj, struct sg_table *sgt)
{
	return dma_get_sgtable(gem_obj->iommu_dev->dev, sgt, gem_obj->virt,
			       qda_gem_dma_addr(gem_obj), gem_obj->size);
}
