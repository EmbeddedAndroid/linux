// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/string.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/qda_accel.h>
#include "qda_drv.h"
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
