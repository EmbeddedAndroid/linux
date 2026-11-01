/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __QDA_GEM_H__
#define __QDA_GEM_H__

#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>

struct qda_iommu_device;
struct qda_memory_manager;
struct qda_session;

/**
 * struct qda_gem_obj - A buffer the DSP can access
 *
 * Either allocated by this driver or imported from another one. Use
 * drm_gem_is_imported() on @base to tell which; imported buffers have no
 * @virt and own no memory, they only hold a mapping of someone else's.
 */
struct qda_gem_obj {
	/** @base: DRM GEM object base; must be first member */
	struct drm_gem_object base;
	/** @iommu_dev: Context bank this buffer is mapped into */
	struct qda_iommu_device *iommu_dev;
	/** @virt: Kernel virtual address, NULL for imported buffers */
	void *virt;
	/** @sgt: Mapping of an imported buffer, NULL otherwise */
	struct sg_table *sgt;
	/** @dma_addr: Address of the buffer as the DSP sees it */
	dma_addr_t dma_addr;
	/** @size: Size of the buffer in bytes */
	size_t size;
};

/**
 * to_qda_gem_obj - Cast a drm_gem_object pointer to qda_gem_obj
 * @gem_obj: Pointer to the embedded drm_gem_object
 */
#define to_qda_gem_obj(gem_obj) container_of(gem_obj, struct qda_gem_obj, base)

extern const struct drm_gem_object_funcs qda_gem_object_funcs;

struct drm_gem_object *qda_gem_create_object(struct drm_device *drm_dev,
					     struct qda_memory_manager *iommu_mgr,
					     size_t size, struct qda_session *session);
int qda_gem_create_handle(struct drm_file *file_priv, struct drm_gem_object *gem_obj, u32 *handle);

#endif /* __QDA_GEM_H__ */
