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
	case QDA_QUERY_DSP_CAPABILITIES:
		return qda_get_dsp_capability(qdev, file_priv->driver_priv,
					      args->cap.attribute_id, &args->cap.capability);
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
	} else if (type == QDA_FASTRPC_RMID_INIT_ATTACH ||
		   type == QDA_FASTRPC_RMID_INIT_CREATE_STATIC) {
		WRITE_ONCE(session->pd, ctx->pd);
	} else if (type == QDA_FASTRPC_RMID_INIT_MUNMAP ||
		   type == QDA_FASTRPC_RMID_INIT_MEM_UNMAP) {
		qda_session_maps_forget(session, type == QDA_FASTRPC_RMID_INIT_MEM_UNMAP, data);
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
 * qda_get_dsp_capability() - Read one DSP capability attribute
 * @qdev: Device whose DSP to query
 * @session: Session the query is issued on if the cache is still empty
 * @attribute_id: Attribute to read
 * @capability: Receives the value of the attribute
 *
 * The attributes are fetched from the DSP once per device and served from the
 * cache afterwards, to every session. The cache is only written by a query
 * that succeeded.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_get_dsp_capability(struct qda_dev *qdev, struct qda_session *session,
			   u32 attribute_id, u32 *capability)
{
	int err;

	if (attribute_id >= QDA_MAX_DSP_ATTRIBUTES)
		return -EOVERFLOW;

	guard(mutex)(&qdev->attr_lock);

	if (!qdev->valid_attributes) {
		err = qda_fastrpc_invoke(QDA_FASTRPC_TYPE_GET_DSP_INFO, qdev->dsp_attributes,
					 session);
		if (err == (int)QDA_FASTRPC_DSP_UNSUPPORTED_API)
			return -EOPNOTSUPP;
		if (err)
			return err;

		qdev->valid_attributes = true;
	}

	*capability = qdev->dsp_attributes[attribute_id];

	return 0;
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

/*
 * The Audio PD keeps the remote heap across daemon restarts, so only the first
 * attach on a device hands it to the DSP. The lock is held across the attach
 * so that a concurrent one cannot skip the heap while the first is still in
 * flight, and the flag is set only once the DSP has accepted it.
 */
static int qda_attach_audio_pd(struct qda_session *session, const char __user *uname, u32 namelen)
{
	struct qda_dev *qdev = session->qda_dev;
	struct qda_static_pd_args sargs = {};
	char *name;
	int err;

	if (strcmp(qdev->dsp_name, "adsp") || !qdev->remote_heap_size)
		return -ENODEV;

	if (!namelen || namelen > QDA_FASTRPC_INIT_NAMELEN_MAX)
		return -EINVAL;

	name = memdup_user(uname, namelen);
	if (IS_ERR(name))
		return PTR_ERR(name);

	if (name[namelen - 1]) {
		err = -EINVAL;
		goto out_free;
	}

	sargs.name = name;
	sargs.namelen = namelen;

	scoped_guard(mutex, &qdev->remote_heap_lock) {
		if (!qdev->audio_init_mem) {
			sargs.heap.addr = qdev->remote_heap_addr;
			sargs.heap.size = qdev->remote_heap_size;
		}

		err = qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_CREATE_STATIC, &sargs, session);
		if (!err && sargs.heap.size)
			qdev->audio_init_mem = true;
	}

out_free:
	kfree(name);

	return err;
}

/**
 * qda_session_attach() - Attach a session to a DSP-resident protection domain
 * @session: Session to attach
 * @pd_type: QDA_ATTACH_ROOT_PD, QDA_ATTACH_SENSORS_PD or QDA_ATTACH_AUDIO_PD
 * @name: User pointer to the static PD name (QDA_ATTACH_AUDIO_PD only)
 * @namelen: Length of @name including the NUL (QDA_ATTACH_AUDIO_PD only)
 *
 * Shared by the accel UAPI and the legacy shim, so the privilege check lives
 * here rather than on the ioctl.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_session_attach(struct qda_session *session, u32 pd_type, const char __user *name,
		       u32 namelen)
{
	int pd;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (READ_ONCE(session->init_mem_gem_obj))
		return -EBUSY;

	switch (pd_type) {
	case QDA_ATTACH_ROOT_PD:
	case QDA_ATTACH_SENSORS_PD:
		if (name || namelen)
			return -EINVAL;

		pd = pd_type == QDA_ATTACH_ROOT_PD ? QDA_ROOT_PD : QDA_SENSORS_PD;

		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_ATTACH, &pd, session);
	case QDA_ATTACH_AUDIO_PD:
		return qda_attach_audio_pd(session, name, namelen);
	default:
		return -EINVAL;
	}
}

/**
 * qda_ioctl_attach() - Attach to a DSP-resident protection domain
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_remote_attach)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_attach(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_qda_remote_attach *args = data;

	return qda_session_attach(file_priv->driver_priv, args->pd_type,
				  u64_to_user_ptr(args->name), args->namelen);
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
	struct qda_mem_map_args args = {
		.map = *map_req,
	};
	int err;

	err = qda_session_mem_map(file_priv->driver_priv, &args);
	if (err)
		return err;

	map_req->vaddrout = args.map.vaddrout;

	return 0;
}

/**
 * qda_session_mem_map() - Map a buffer into the DSP's address space
 * @session: Session to map for
 * @args: Mapping parameters; @args->map.vaddrout is filled in on success
 *
 * For sessions with no handle table to name the buffer by, such as those of
 * the legacy shim, which supply @args->obj.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_session_mem_map(struct qda_session *session, struct qda_mem_map_args *args)
{
	switch (args->map.request) {
	case QDA_REQUEST_MMAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MMAP, args, session);
	case QDA_REQUEST_MEM_MAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MEM_MAP, args, session);
	default:
		return -EINVAL;
	}
}

/**
 * qda_session_mem_unmap() - Remove a buffer's mapping from the DSP's address space
 * @session: Session the mapping belongs to
 * @unmap: Which mapping to remove
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_session_mem_unmap(struct qda_session *session, struct drm_qda_mem_unmap *unmap)
{
	switch (unmap->request) {
	case QDA_REQUEST_MUNMAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MUNMAP, unmap, session);
	case QDA_REQUEST_MEM_UNMAP:
		return qda_fastrpc_invoke(QDA_FASTRPC_RMID_INIT_MEM_UNMAP, unmap, session);
	default:
		return -EINVAL;
	}
}

/**
 * qda_ioctl_munmap() - Remove a buffer's mapping from the DSP's address space
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_mem_unmap)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_munmap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return qda_session_mem_unmap(file_priv->driver_priv, data);
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

/**
 * qda_session_set_option() - Set a per-session option
 * @session: Session to update
 * @request_id: Option to set (QDA_SET_OPTION_*)
 * @value: Request-specific value
 *
 * Shared by the accel UAPI and the legacy shim's FASTRPC_IOCTL_SET_OPTION.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_session_set_option(struct qda_session *session, u32 request_id, u32 value)
{
	switch (request_id) {
	case QDA_SET_OPTION_POLL_MODE:
		if (!session->qda_dev->poll_mode_supported)
			return -EOPNOTSUPP;

		if (value == QDA_POLL_MODE_ENABLE)
			WRITE_ONCE(session->poll_mode, true);
		else if (value == QDA_POLL_MODE_DISABLE)
			WRITE_ONCE(session->poll_mode, false);
		else
			return -EINVAL;

		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * qda_ioctl_set_option() - Set a per-session option
 * @dev: DRM device structure
 * @data: User-space data (struct drm_qda_set_option)
 * @file_priv: DRM file private data
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_ioctl_set_option(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_qda_set_option *args = data;

	return qda_session_set_option(file_priv->driver_priv, args->request_id, args->value);
}
