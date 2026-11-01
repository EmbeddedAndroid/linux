// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>
#include <drm/qda_accel.h>

#include "qda_drv.h"
#include "qda_ioctl.h"
#include "qda_prime.h"

/**
 * qda_session_alloc() - Create a session on a device
 * @qdev: Device to create the session on
 *
 * Return: The new session, or ERR_PTR on failure
 */
struct qda_session *qda_session_alloc(struct qda_dev *qdev)
{
	struct qda_session *session;

	session = kzalloc_obj(*session);
	if (!session)
		return ERR_PTR(-ENOMEM);

	session->qda_dev = qdev;

	return session;
}
EXPORT_SYMBOL_FOR_MODULES(qda_session_alloc, "qda_fastrpc_shim");

static int qda_open(struct drm_device *dev, struct drm_file *file)
{
	struct qda_dev *qdev = qda_dev_from_drm(dev);
	struct qda_session *session;

	session = qda_session_alloc(qdev);
	if (IS_ERR(session))
		return PTR_ERR(session);

	session->file = file;
	file->driver_priv = session;

	return 0;
}

/**
 * qda_session_free() - Release a session and whatever it still holds
 * @session: Session to release
 */
void qda_session_free(struct qda_session *session)
{
	if (session->assigned_iommu_dev)
		qda_memory_manager_release_device(session->assigned_iommu_dev);

	kfree(session);
}
EXPORT_SYMBOL_FOR_MODULES(qda_session_free, "qda_fastrpc_shim");

static void qda_postclose(struct drm_device *dev, struct drm_file *file)
{
	qda_session_free(file->driver_priv);
}

DEFINE_DRM_ACCEL_FOPS(qda_accel_fops);

static const struct drm_ioctl_desc qda_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QDA_QUERY, qda_ioctl_query, 0),
	DRM_IOCTL_DEF_DRV(QDA_GEM_CREATE, qda_ioctl_gem_create, 0),
	DRM_IOCTL_DEF_DRV(QDA_GEM_MMAP_OFFSET, qda_ioctl_gem_mmap_offset, 0),
};

static const struct drm_driver qda_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL,
	.fops = &qda_accel_fops,
	.open = qda_open,
	.postclose = qda_postclose,
	.ioctls = qda_ioctls,
	.num_ioctls = ARRAY_SIZE(qda_ioctls),
	.gem_prime_import = qda_gem_prime_import,
	.gem_prime_import_sg_table = qda_gem_prime_import_sg_table,
	.prime_fd_to_handle = qda_prime_fd_to_handle,
	.name = QDA_DRIVER_NAME,
	.desc = "Qualcomm DSP Accelerator Driver",
};

struct qda_dev *qda_alloc_device(struct device *dev)
{
	struct qda_dev *qdev;

	qdev = devm_drm_dev_alloc(dev, &qda_drm_driver, struct qda_dev, drm_dev);
	if (IS_ERR(qdev))
		return ERR_CAST(qdev);

	INIT_LIST_HEAD(&qdev->cb_devs);
	mutex_init(&qdev->import_lock);

	return qdev;
}

int qda_init_device(struct qda_dev *qdev, int num_cbs)
{
	int ret;

	qdev->iommu_mgr = devm_kzalloc(qdev->dev, sizeof(*qdev->iommu_mgr), GFP_KERNEL);
	if (!qdev->iommu_mgr)
		return -ENOMEM;

	ret = qda_memory_manager_init(qdev->iommu_mgr, num_cbs);
	if (ret)
		drm_err(&qdev->drm_dev, "Failed to initialize memory manager: %d\n", ret);

	return ret;
}

void qda_deinit_device(struct qda_dev *qdev)
{
	qda_memory_manager_exit(qdev->iommu_mgr);
	mutex_destroy(&qdev->import_lock);
}

int qda_register_device(struct qda_dev *qdev)
{
	int ret;

	ret = drm_dev_register(&qdev->drm_dev, 0);
	if (ret) {
		drm_err(&qdev->drm_dev, "Failed to register DRM device: %d\n", ret);
		return ret;
	}

	/*
	 * The legacy FastRPC interface is served by a separate module, which
	 * needs a device per DSP domain to bind to. Put one on the auxiliary
	 * bus and let the driver core handle discovery, ordering and module
	 * loading; the module is optional, so failing to create it is not
	 * fatal to the accel interface.
	 *
	 * The auxiliary bus names devices <modname>.<name>.<id> and requires
	 * the name to be unique across the bus, so every DSP domain needs its
	 * own id. The accel minor index is unique per registered device.
	 */
	qdev->legacy_auxdev = auxiliary_device_create(qdev->dev, KBUILD_MODNAME,
						      QDA_LEGACY_AUXDEV_NAME, qdev,
						      qdev->drm_dev.accel->index);
	if (!qdev->legacy_auxdev)
		drm_warn(&qdev->drm_dev, "Failed to create legacy FastRPC device\n");

	return 0;
}

void qda_unregister_device(struct qda_dev *qdev)
{
	if (qdev->legacy_auxdev) {
		auxiliary_device_destroy(qdev->legacy_auxdev);
		qdev->legacy_auxdev = NULL;
	}
}

MODULE_AUTHOR("Qualcomm AI Infra Team");
MODULE_DESCRIPTION("Qualcomm DSP Accelerator Driver");
MODULE_LICENSE("GPL");
