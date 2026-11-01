// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/string.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/qda_accel.h>
#include "qda_drv.h"
#include "qda_fastrpc.h"
#include "qda_gem.h"
#include "qda_ioctl.h"

/**
 * qda_ioctl_query() - Query a property of the DSP
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_query)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_query(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qda_dev *qdev = qda_dev_from_drm(dev);
	struct drm_qda_query *args = data;

	switch (args->query_type) {
	case QDA_QUERY_DSP_NAME:
		strscpy(args->dsp_name, qdev->dsp_name, sizeof(args->dsp_name));
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * qda_ioctl_gem_create() - Allocate a buffer the DSP can access
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_gem_create)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_gem_create(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qda_dev *qdev = qda_dev_from_drm(dev);
	struct drm_qda_gem_create *args = data;
	struct drm_gem_object *gem_obj;

	if (args->pad)
		return -EINVAL;

	gem_obj = qda_gem_create_object(dev, qdev->iommu_mgr, args->size,
					file_priv->driver_priv);
	if (IS_ERR(gem_obj))
		return PTR_ERR(gem_obj);

	return qda_gem_create_handle(file_priv, gem_obj, &args->handle);
}

/**
 * qda_ioctl_gem_mmap_offset() - Get the mmap offset of a buffer
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_gem_mmap_offset)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_gem_mmap_offset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_qda_gem_mmap_offset *args = data;

	if (args->pad)
		return -EINVAL;

	return drm_gem_dumb_map_offset(file_priv, dev, args->handle, &args->offset);
}

/*
 * A process created again replaces the first, so the memory held for the
 * previous one is dropped. Swapped atomically, as ioctls on one file can run
 * concurrently.
 */
static void qda_session_set_init_mem(struct qda_session *session, struct qda_gem_obj *mem)
{
	struct qda_gem_obj *old;

	drm_gem_object_get(&mem->base);
	old = xchg(&session->init_mem_gem_obj, mem);
	if (old)
		drm_gem_object_put(&old->base);
}

static int qda_fastrpc_invoke(u32 type, void *data, struct qda_session *session)
{
	struct qda_dev *qdev = session->qda_dev;
	struct qda_fastrpc_invoke_ctx *ctx;
	int err;

	ctx = qda_fastrpc_context_alloc(qdev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->type = type;

	err = qda_fastrpc_prepare_args(ctx, session, data);
	if (err)
		goto out_put;

	err = qda_fastrpc_invoke_common(qdev, ctx);
	if (err)
		goto out_put;

	qda_fastrpc_return_result(ctx, data);

	/*
	 * The session only changes once the DSP has accepted the request, so a
	 * failed create leaves the previous process, if any, as it was. The
	 * memory is freed with the session, which the invocation keeps alive
	 * until it is done, so a release needs no reference of its own.
	 */
	if (type == QDA_FASTRPC_RMID_INIT_CREATE) {
		qda_session_set_init_mem(session, ctx->init_mem_gem_obj);
		WRITE_ONCE(session->pd, QDA_USER_PD);
	}

out_put:
	kref_put(&ctx->refcount, qda_fastrpc_context_free);

	return err;
}

/**
 * qda_ioctl_init_create() - Create a DSP process
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_init_create)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_init_create(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct qda_init_create_args create = {
		.init = *(struct drm_qda_init_create *)data,
	};

	return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_CREATE, &create, file_priv->driver_priv);
}

/**
 * qda_session_init_create() - Create a DSP process from an imported ELF buffer
 * @session: Session to create the process for
 * @create: Creation parameters; @create->init.filehandle and @create->init.file
 *          must be zero
 *
 * For sessions with no handle table to name the ELF buffer by, such as those
 * of the legacy shim.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_session_init_create(struct qda_session *session, struct qda_init_create_args *create)
{
	return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_CREATE, create, session);
}

/**
 * qda_release_dsp_process() - Tear down a session's DSP process
 * @qdev: QDA device structure
 * @session: Session whose process is released
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_release_dsp_process(struct qda_dev *qdev, struct qda_session *session)
{
	return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_RELEASE, NULL, session);
}

/**
 * qda_ioctl_mmap() - Map a buffer into the DSP's address space
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_mem_map)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_mmap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_qda_mem_map *map_req = data;

	switch (map_req->request) {
	case QDA_REQUEST_MMAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MMAP, data, file_priv->driver_priv);
	case QDA_REQUEST_MEM_MAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MEM_MAP, data,
					  file_priv->driver_priv);
	default:
		return -EINVAL;
	}
}

/**
 * qda_ioctl_invoke() - Run a method on the DSP
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_invoke_args)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_invoke(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INVOKE_DYNAMIC, data, file_priv->driver_priv);
}
