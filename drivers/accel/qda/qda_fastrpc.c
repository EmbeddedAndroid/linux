// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/bitfield.h>
#include <linux/slab.h>
#include "qda_drv.h"
#include "qda_fastrpc.h"

/**
 * qda_fastrpc_context_free() - Release an invocation
 * @ref: Reference counter embedded in the invocation
 *
 * Runs when the last reference goes. The invocation leaves the lookup table
 * here and not earlier, so that its ID cannot be handed to a new invocation
 * while this one can still be found and completed.
 */
void qda_fastrpc_context_free(struct kref *ref)
{
	struct qda_fastrpc_invoke_ctx *ctx =
		container_of(ref, struct qda_fastrpc_invoke_ctx, refcount);

	xa_erase(&ctx->qdev->ctx_xa, FIELD_GET(QDA_FASTRPC_CTXID_MASK, ctx->ctxid));
	kfree(ctx);
}

static void qda_fastrpc_context_put_wq(struct work_struct *work)
{
	struct qda_fastrpc_invoke_ctx *ctx =
		container_of(work, struct qda_fastrpc_invoke_ctx, put_work);

	kref_put(&ctx->refcount, qda_fastrpc_context_free);
}

/**
 * qda_fastrpc_context_alloc() - Allocate an invocation and give it an ID
 * @qdev: Device the invocation is issued on
 *
 * The caller holds the initial reference and drops it with
 * qda_fastrpc_context_free().
 *
 * Return: The new invocation, or ERR_PTR on failure
 */
struct qda_fastrpc_invoke_ctx *qda_fastrpc_context_alloc(struct qda_dev *qdev)
{
	struct qda_fastrpc_invoke_ctx *ctx;
	u32 id;
	int ret;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	init_completion(&ctx->work);
	INIT_WORK(&ctx->put_work, qda_fastrpc_context_put_wq);
	kref_init(&ctx->refcount);
	ctx->qdev = qdev;
	/* Anything but a status the DSP could send, until it answers */
	ctx->retval = -1;

	/* ID 0 is never used, so a zeroed context in a response matches nothing */
	ret = xa_alloc(&qdev->ctx_xa, &id, ctx, XA_LIMIT(1, QDA_FASTRPC_CTXID_MAX),
		       GFP_KERNEL);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	ctx->ctxid = FIELD_PREP(QDA_FASTRPC_CTXID_MASK, id) |
		     FIELD_PREP(QDA_FASTRPC_CTXID_SEQ_MASK,
				(u64)atomic64_inc_return(&qdev->ctx_seq));

	return ctx;
}
