// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rpmsg.h>
#include <linux/wait.h>
#include <drm/drm_print.h>

#include "qda_cb.h"
#include "qda_drv.h"
#include "qda_fastrpc.h"
#include "qda_rpmsg.h"
#include "qda_soc.h"

/*
 * Both compatibles the binding defines are matched here. The glymur, hawi,
 * maili and nord nodes name "qcom,kaanapali-fastrpc" as their fallback
 * compatible and so are matched by that entry.
 */
static const struct of_device_id qda_rpmsg_of_match[] = {
	{ .compatible = "qcom,kaanapali-fastrpc", .data = &kaanapali_soc_data },
	{ .compatible = "qcom,fastrpc", .data = &default_soc_data },
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

/**
 * qda_rpmsg_send_msg() - Send a packed invocation to the remote processor
 * @qdev: QDA device structure
 * @msg: Packed message to send
 *
 * Takes a reference on the invocation context on behalf of the response
 * callback, which keeps the context alive until the DSP has replied.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_rpmsg_send_msg(struct qda_dev *qdev, struct qda_msg *msg)
{
	struct qda_fastrpc_invoke_ctx *ctx = msg->fastrpc_ctx;
	int ret, idx;

	if (!drm_dev_enter(&qdev->drm_dev, &idx))
		return -ENODEV;

	if (!qdev->rpdev) {
		ret = -ENODEV;
		goto out_exit;
	}

	kref_get(&ctx->refcount);

	ret = rpmsg_send(qdev->rpdev->ept, &msg->fastrpc, sizeof(msg->fastrpc));
	if (ret) {
		drm_err_ratelimited(&qdev->drm_dev, "Failed to send rpmsg: %d\n", ret);
		kref_put(&ctx->refcount, qda_fastrpc_context_free);
	}

out_exit:
	drm_dev_exit(idx);

	return ret;
}

/**
 * qda_rpmsg_wait_for_rsp() - Wait for the DSP to answer an invocation
 * @ctx: FastRPC invocation context
 *
 * Return: 0 once the response arrived, or -ERESTARTSYS if a signal
 * interrupted the wait
 */
int qda_rpmsg_wait_for_rsp(struct qda_fastrpc_invoke_ctx *ctx)
{
	return wait_for_completion_interruptible(&ctx->work);
}

static int qda_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct qda_dev *qdev = dev_get_drvdata(&rpdev->dev);
	struct qda_invoke_rsp *resp_msg = data;
	struct qda_fastrpc_invoke_ctx *ctx;
	int ret = 0, idx;

	if (!qdev)
		return -ENODEV;

	if (!drm_dev_enter(&qdev->drm_dev, &idx))
		return -ENODEV;

	if (len < sizeof(*resp_msg)) {
		ret = -EINVAL;
		goto out_exit;
	}

	ctx = xa_load(&qdev->ctx_xa, FIELD_GET(QDA_FASTRPC_CTXID_MASK, resp_msg->ctx));
	if (!ctx) {
		drm_dbg_driver(&qdev->drm_dev, "No context matches response 0x%llx\n",
			       resp_msg->ctx);
		ret = -ENOENT;
		goto out_exit;
	}

	/*
	 * Context IDs are only 8 bits wide and are recycled once a context is
	 * freed, so a late or duplicate response may name an ID that now belongs
	 * to a newer context. The sequence number tells the two apart.
	 */
	if (FIELD_GET(QDA_FASTRPC_CTXID_SEQ_MASK, resp_msg->ctx) !=
	    FIELD_GET(QDA_FASTRPC_CTXID_SEQ_MASK, ctx->ctxid)) {
		drm_dbg_driver(&qdev->drm_dev, "Dropping stale response 0x%llx\n",
			       resp_msg->ctx);
		goto out_exit;
	}

	ctx->retval = resp_msg->retval;
	complete(&ctx->work);

	/*
	 * Release the reference taken by qda_rpmsg_send_msg(). This callback
	 * runs in atomic context, and if it drops the last reference the
	 * context's DMA buffers are freed, which must not happen here.
	 */
	schedule_work(&ctx->put_work);

out_exit:
	drm_dev_exit(idx);

	return ret;
}

static void qda_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct qda_dev *qdev = dev_get_drvdata(&rpdev->dev);

	/* Take the legacy nodes away first, so nothing new opens during teardown */
	qda_unregister_device(qdev);

	/*
	 * Unplug before clearing @rpdev: once there are ioctls that reach the
	 * DSP, this is what stops a new one from starting and waits for the
	 * ones already running, so that none of them sees a transport that has
	 * gone away.
	 */
	drm_dev_unplug(&qdev->drm_dev);
	qdev->rpdev = NULL;
	qda_fastrpc_flush_pending(qdev);
	qda_cb_unpopulate(qdev);
	qda_deinit_device(qdev);
}

static int qda_count_cb_nodes(struct device_node *parent_node)
{
	int count = 0;

	for_each_available_child_of_node_scoped(parent_node, child)
		if (of_device_is_compatible(child, "qcom,fastrpc-compute-cb"))
			count++;

	return count;
}

static int qda_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct qda_dev *qdev;
	int ret;

	qdev = qda_rpmsg_alloc_and_init_qdev(rpdev);
	if (IS_ERR(qdev))
		return PTR_ERR(qdev);

	qdev->soc_data = of_device_get_match_data(&rpdev->dev);
	if (!qdev->soc_data)
		return -ENODEV;

	ret = of_property_read_string(rpdev->dev.of_node, "label", &qdev->dsp_name);
	if (ret) {
		dev_err(&rpdev->dev, "Missing 'label' property in DT node: %d\n", ret);
		return ret;
	}

	ret = qda_init_device(qdev, qda_count_cb_nodes(rpdev->dev.of_node));
	if (ret)
		return ret;

	ret = qda_cb_populate(qdev, rpdev->dev.of_node);
	if (ret)
		goto err_deinit;

	ret = qda_register_device(qdev);
	if (ret)
		goto err_unpopulate;

	return 0;

err_unpopulate:
	qda_cb_unpopulate(qdev);
err_deinit:
	qda_deinit_device(qdev);
	return ret;
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
