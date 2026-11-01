// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/module.h>
#include <linux/slab.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>

#include "qda_drv.h"

static struct qda_session *qda_session_alloc(struct qda_dev *qdev)
{
	struct qda_session *session;

	session = kzalloc_obj(*session);
	if (!session)
		return ERR_PTR(-ENOMEM);

	session->qda_dev = qdev;

	return session;
}

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

static void qda_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct qda_session *session = file->driver_priv;

	kfree(session);
}

DEFINE_DRM_ACCEL_FOPS(qda_accel_fops);

static const struct drm_driver qda_drm_driver = {
	.driver_features = DRIVER_COMPUTE_ACCEL,
	.fops = &qda_accel_fops,
	.open = qda_open,
	.postclose = qda_postclose,
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

	return qdev;
}

int qda_register_device(struct qda_dev *qdev)
{
	int ret;

	ret = drm_dev_register(&qdev->drm_dev, 0);
	if (ret)
		drm_err(&qdev->drm_dev, "Failed to register DRM device: %d\n", ret);

	return ret;
}

MODULE_AUTHOR("Qualcomm AI Infra Team");
MODULE_DESCRIPTION("Qualcomm DSP Accelerator Driver");
MODULE_LICENSE("GPL");
