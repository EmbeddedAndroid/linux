// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rpmsg.h>
#include <drm/drm_print.h>

#include "qda_drv.h"

/*
 * Both compatibles the binding defines are matched here. The glymur, hawi,
 * maili and nord nodes name "qcom,kaanapali-fastrpc" as their fallback
 * compatible and so are matched by that entry. Per-SoC DSP parameters are
 * attached as match data by a later patch, once there is something that
 * consumes them.
 */
static const struct of_device_id qda_rpmsg_of_match[] = {
	{ .compatible = "qcom,kaanapali-fastrpc" },
	{ .compatible = "qcom,fastrpc" },
	{ }
};
MODULE_DEVICE_TABLE(of, qda_rpmsg_of_match);

static struct qda_dev *qda_rpmsg_alloc_and_init_qdev(struct rpmsg_device *rpdev)
{
	struct qda_dev *qdev;

	qdev = qda_alloc_device(&rpdev->dev);
	if (IS_ERR(qdev))
		return qdev;

	qdev->dev = &rpdev->dev;
	qdev->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, qdev);

	return qdev;
}

/*
 * The rpmsg core requires a callback, so one is registered from the start.
 * Responses are dispatched here once there are invocations to answer.
 */
static int qda_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	return 0;
}

static void qda_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct qda_dev *qdev = dev_get_drvdata(&rpdev->dev);

	/*
	 * Unplug before clearing @rpdev: once there are ioctls that reach the
	 * DSP, this is what stops a new one from starting and waits for the
	 * ones already running, so that none of them sees a transport that has
	 * gone away.
	 */
	drm_dev_unplug(&qdev->drm_dev);
	qdev->rpdev = NULL;
}

static int qda_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct qda_dev *qdev;
	int ret;

	qdev = qda_rpmsg_alloc_and_init_qdev(rpdev);
	if (IS_ERR(qdev))
		return PTR_ERR(qdev);

	ret = of_property_read_string(rpdev->dev.of_node, "label", &qdev->dsp_name);
	if (ret) {
		dev_err(&rpdev->dev, "Missing 'label' property in DT node: %d\n", ret);
		return ret;
	}

	return qda_register_device(qdev);
}

static struct rpmsg_driver qda_rpmsg_driver = {
	.probe = qda_rpmsg_probe,
	.remove = qda_rpmsg_remove,
	.callback = qda_rpmsg_cb,
	.drv = {
		.name = QDA_DRIVER_NAME,
		.of_match_table = qda_rpmsg_of_match,
	},
};

module_rpmsg_driver(qda_rpmsg_driver);
