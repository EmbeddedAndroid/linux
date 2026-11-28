// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>
#include "qda_drv.h"
#include "qda_gem.h"
#include "qda_memory_manager.h"
#include "qda_prime.h"
#include "qda_soc.h"

/**
 * qda_gem_prime_import_sg_table() - Wrap an imported buffer in a GEM object
 * @drm_dev: DRM device
 * @attach: Attachment the DRM core made to the exporter
 * @sgt: Mapping of the buffer in the context bank's address space
 *
 * Called by the DRM PRIME helpers once they have attached to the exporter
 * and mapped the buffer into the context bank this session owns.
 *
 * The DSP is told where a buffer is by a single address, so only buffers
 * that map as one contiguous range can be described to it. Whether an
 * exporter or an IOMMU produces a single range is not guaranteed, so check
 * rather than assume: a buffer mapped as several ranges would otherwise be
 * handed to the DSP as its first range alone, leaving the DSP to read and
 * write past the end of it into whatever happens to follow in the context
 * bank.
 *
 * Return: The new GEM object on success, ERR_PTR on failure
 */
struct drm_gem_object *qda_gem_prime_import_sg_table(struct drm_device *drm_dev,
						     struct dma_buf_attachment *attach,
						     struct sg_table *sgt)
{
	struct qda_dev *qdev = qda_dev_from_drm(drm_dev);
	size_t size = PAGE_ALIGN(attach->dmabuf->size);
	struct qda_iommu_device *iommu_dev;
	struct qda_gem_obj *qda_gem_obj;
	int ret;

	if (drm_prime_get_contiguous_size(sgt) < size) {
		drm_dbg_driver(drm_dev, "Rejecting non-contiguous buffer of %zu bytes\n", size);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * The buffer was mapped into the context bank of the session doing the
	 * import, so that is the bank the DSP has to be told about.
	 */
	iommu_dev = dev_get_drvdata(attach->dev);
	if (!iommu_dev)
		return ERR_PTR(-ENODEV);

	qda_gem_obj = kzalloc_obj(*qda_gem_obj);
	if (!qda_gem_obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(drm_dev, &qda_gem_obj->base, size);
	if (ret) {
		kfree(qda_gem_obj);
		return ERR_PTR(ret);
	}

	qda_gem_obj->base.funcs = &qda_gem_object_funcs;
	qda_gem_obj->size = size;
	qda_gem_obj->sgt = sgt;
	qda_gem_obj->iommu_dev = iommu_dev;
	qda_gem_obj->dma_addr = sg_dma_address(sgt->sgl) +
				qda_soc_sid_offset(qdev->soc_data, iommu_dev->sid);
	qda_memory_manager_get_device(iommu_dev);

	return &qda_gem_obj->base;
}

/**
 * qda_gem_prime_import() - Import a DMA-BUF into the calling session
 * @drm_dev: DRM device
 * @dma_buf: Buffer to import
 *
 * A buffer has to be mapped into the context bank of the session importing
 * it, rather than into the address space of the parent device, so the
 * attachment is made to that bank.
 *
 * drm_gem_prime_fd_to_handle() does not pass the calling file down to this
 * callback, so the session is recovered from @qda_dev.importing_session,
 * which qda_prime_fd_to_handle() publishes for the duration of the call.
 *
 * Return: The new GEM object on success, ERR_PTR on failure
 */
struct drm_gem_object *qda_gem_prime_import(struct drm_device *drm_dev,
					    struct dma_buf *dma_buf)
{
	struct qda_dev *qdev = qda_dev_from_drm(drm_dev);
	struct qda_session *session = qdev->importing_session;
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;
	struct sg_table *sgt;
	int ret;

	if (!session || !session->assigned_iommu_dev)
		return ERR_PTR(-EINVAL);

	if (drm_gem_is_prime_exported_dma_buf(drm_dev, dma_buf)) {
		/*
		 * A buffer this driver exported. The DRM core would hand back
		 * the original object, which stays in the context bank of the
		 * session that allocated it, while the DSP of this session
		 * translates addresses in its own bank. Share the object only
		 * within a bank, and map it again for any other.
		 */
		obj = dma_buf->priv;
		if (to_qda_gem_obj(obj)->iommu_dev == session->assigned_iommu_dev) {
			drm_gem_object_get(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(dma_buf, session->assigned_iommu_dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_detach;
	}

	obj = qda_gem_prime_import_sg_table(drm_dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto err_unmap;
	}

	obj->import_attach = attach;
	obj->resv = dma_buf->resv;

	return obj;

err_unmap:
	dma_buf_unmap_attachment_unlocked(attach, sgt, DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

/**
 * qda_prime_fd_to_handle() - Import a DMA-BUF into a session
 * @drm_dev: DRM device
 * @file_priv: DRM file doing the import
 * @prime_fd: File descriptor of the buffer to import
 * @handle: Returned handle for the buffer
 *
 * Makes sure the session owns a context bank to map the buffer into, then
 * publishes it for qda_gem_prime_import() to pick up.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_prime_fd_to_handle(struct drm_device *drm_dev, struct drm_file *file_priv,
			   int prime_fd, u32 *handle)
{
	struct qda_dev *qdev = qda_dev_from_drm(drm_dev);
	struct qda_session *session = file_priv->driver_priv;
	int ret;

	ret = qda_memory_manager_assign_device(qdev->iommu_mgr, session);
	if (ret)
		return ret;

	guard(mutex)(&qdev->import_lock);

	qdev->importing_session = session;
	ret = drm_gem_prime_fd_to_handle(drm_dev, file_priv, prime_fd, handle);
	qdev->importing_session = NULL;

	return ret;
}

/**
 * qda_prime_import_dmabuf() - Import a DMA-BUF without installing a handle
 * @drm_dev: DRM device
 * @session: Session to import into
 * @dma_buf: Buffer to import
 *
 * As qda_prime_fd_to_handle(), but stopping at the GEM object: a session the
 * legacy shim created has no DRM file to install a handle in. The caller owns
 * the returned reference.
 *
 * Return: The new GEM object on success, ERR_PTR on failure
 */
struct drm_gem_object *qda_prime_import_dmabuf(struct drm_device *drm_dev,
					       struct qda_session *session,
					       struct dma_buf *dma_buf)
{
	struct qda_dev *qdev = qda_dev_from_drm(drm_dev);
	struct drm_gem_object *gem_obj;
	int ret;

	ret = qda_memory_manager_assign_device(qdev->iommu_mgr, session);
	if (ret)
		return ERR_PTR(ret);

	guard(mutex)(&qdev->import_lock);

	qdev->importing_session = session;
	gem_obj = qda_gem_prime_import(drm_dev, dma_buf);
	qdev->importing_session = NULL;

	return gem_obj;
}

MODULE_IMPORT_NS("DMA_BUF");
