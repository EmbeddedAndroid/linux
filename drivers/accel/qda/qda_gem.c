// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <drm/drm_gem.h>
#include "qda_drv.h"
#include "qda_gem.h"
#include "qda_memory_dma.h"
#include "qda_memory_manager.h"

static void qda_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct qda_gem_obj *qda_gem_obj = to_qda_gem_obj(gem_obj);

	if (qda_gem_obj->virt)
		qda_memory_manager_free(qda_gem_obj);

	drm_gem_object_release(gem_obj);
	kfree(qda_gem_obj);
}

static int qda_gem_mmap_obj(struct drm_gem_object *gem_obj, struct vm_area_struct *vma)
{
	struct qda_gem_obj *qda_gem_obj = to_qda_gem_obj(gem_obj);
	int ret;

	/* The fake offset only identifies the object, it does not index into it */
	vma->vm_pgoff = 0;

	ret = qda_dma_mmap(qda_gem_obj, vma);
	if (ret)
		return ret;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);

	return 0;
}

static const struct drm_gem_object_funcs qda_gem_object_funcs = {
	.free = qda_gem_free_object,
	.mmap = qda_gem_mmap_obj,
};

/**
 * qda_gem_create_object() - Allocate a buffer the DSP can access
 * @drm_dev: DRM device
 * @iommu_mgr: Memory manager to allocate from
 * @size: Requested size in bytes
 * @session: Session the buffer belongs to
 *
 * Return: The new GEM object on success, ERR_PTR on failure
 */
struct drm_gem_object *qda_gem_create_object(struct drm_device *drm_dev,
					     struct qda_memory_manager *iommu_mgr,
					     size_t size, struct qda_session *session)
{
	struct qda_gem_obj *qda_gem_obj;
	size_t aligned_size;
	int ret;

	aligned_size = PAGE_ALIGN(size);
	if (!aligned_size)
		return ERR_PTR(-EINVAL);

	qda_gem_obj = kzalloc_obj(*qda_gem_obj);
	if (!qda_gem_obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(drm_dev, &qda_gem_obj->base, aligned_size);
	if (ret) {
		kfree(qda_gem_obj);
		return ERR_PTR(ret);
	}

	qda_gem_obj->base.funcs = &qda_gem_object_funcs;
	qda_gem_obj->size = aligned_size;

	ret = qda_memory_manager_alloc(iommu_mgr, qda_gem_obj, session);
	if (ret) {
		drm_gem_object_release(&qda_gem_obj->base);
		kfree(qda_gem_obj);
		return ERR_PTR(ret);
	}

	return &qda_gem_obj->base;
}

/**
 * qda_gem_create_handle() - Give user space a handle to a GEM object
 * @file_priv: DRM file to install the handle in
 * @gem_obj: Object to install
 * @handle: Returned handle
 *
 * Consumes the caller's reference on @gem_obj: the handle holds one of its
 * own, so on success the object stays alive until user space closes it, and
 * on failure it is destroyed here.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_gem_create_handle(struct drm_file *file_priv, struct drm_gem_object *gem_obj, u32 *handle)
{
	int ret;

	ret = drm_gem_handle_create(file_priv, gem_obj, handle);
	drm_gem_object_put(gem_obj);

	return ret;
}
