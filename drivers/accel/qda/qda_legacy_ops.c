// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fcntl.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include "qda_drv.h"
#include "qda_gem.h"
#include "qda_legacy_ops.h"
#include "qda_prime.h"

/*
 * A session the shim creates has no DRM file behind it, and so nothing
 * holding the DRM device. Take a reference here: the device is allocated
 * against the rpmsg device and would otherwise be freed on remove while a
 * /dev/fastrpc-* file is still open on it.
 */
static struct qda_session *legacy_session_create(struct qda_dev *qdev)
{
	struct qda_session *session;

	session = qda_session_alloc(qdev);
	if (!IS_ERR(session))
		drm_dev_get(&qdev->drm_dev);

	return session;
}

static void legacy_session_destroy(struct qda_session *session)
{
	struct drm_device *drm_dev = &session->qda_dev->drm_dev;

	qda_session_put(session);
	drm_dev_put(drm_dev);
}

/*
 * As the GEM_CREATE ioctl, but exporting a DMA-BUF rather than installing a
 * handle: a session the shim created has no DRM file to hold handles, and the
 * legacy interface hands buffers to user space as file descriptors anyway.
 */
static struct dma_buf *legacy_alloc_dmabuf(struct qda_session *session, u64 size)
{
	struct qda_dev *qdev = session->qda_dev;
	struct drm_gem_object *gem_obj;
	struct dma_buf *dmabuf;

	gem_obj = qda_gem_create_object(&qdev->drm_dev, qdev->iommu_mgr, size, session);
	if (IS_ERR(gem_obj))
		return ERR_CAST(gem_obj);

	/* O_RDWR so the fd can be mapped writable */
	dmabuf = drm_gem_prime_export(gem_obj, O_RDWR);

	/* The DMA-BUF took its own reference if the export succeeded */
	drm_gem_object_put(gem_obj);

	return dmabuf;
}

/*
 * As the PRIME import path, without the handle: same reason as above. The
 * caller owns the returned reference.
 */
static struct drm_gem_object *legacy_import_dmabuf(struct qda_session *session,
						   struct dma_buf *dmabuf)
{
	return qda_prime_import_dmabuf(&session->qda_dev->drm_dev, session, dmabuf);
}

const struct qda_legacy_ops qda_legacy_ops_table = {
	.session_create = legacy_session_create,
	.session_destroy = legacy_session_destroy,
	.alloc_dmabuf = legacy_alloc_dmabuf,
	.import_dmabuf = legacy_import_dmabuf,
};
EXPORT_SYMBOL_FOR_MODULES(qda_legacy_ops_table, "qda_fastrpc_shim");
