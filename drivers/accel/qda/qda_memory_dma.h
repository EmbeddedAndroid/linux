/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_MEMORY_DMA_H__
#define __QDA_MEMORY_DMA_H__

#include <linux/mm_types.h>
#include <linux/types.h>

struct sg_table;
struct qda_gem_obj;
struct qda_iommu_device;

int qda_dma_alloc(struct qda_iommu_device *iommu_dev,
		  struct qda_gem_obj *gem_obj, size_t size);
void qda_dma_free(struct qda_gem_obj *gem_obj);
int qda_dma_mmap(struct qda_gem_obj *gem_obj, struct vm_area_struct *vma);
int qda_dma_get_sgtable(struct qda_gem_obj *gem_obj, struct sg_table *sgt);

#endif /* __QDA_MEMORY_DMA_H__ */
