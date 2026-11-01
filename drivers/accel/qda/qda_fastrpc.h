/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_FASTRPC_H__
#define __QDA_FASTRPC_H__

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct qda_dev;

/*
 * Layout of the context the DSP echoes back in its response:
 *   [63:16] sequence number
 *   [15:8]  context ID, the key in the device's lookup table
 *   [7:0]   reserved for the protection domain, set when the message is built
 *
 * The context ID is narrow and is reused as soon as its invocation is freed,
 * so a late or duplicate response could name an ID that now belongs to a
 * newer invocation. The sequence number, which never repeats in practice,
 * lets the response handler tell the two apart.
 */
#define QDA_FASTRPC_CTXID_MASK		GENMASK_ULL(15, 8)
#define QDA_FASTRPC_CTXID_SEQ_MASK	GENMASK_ULL(63, 16)
#define QDA_FASTRPC_CTXID_MAX		(QDA_FASTRPC_CTXID_MASK >> 8)

/**
 * struct qda_fastrpc_msg - FastRPC wire message for remote invocations
 *
 * Sent to the remote processor via RPMsg. This is the exact layout
 * the DSP expects; do not reorder or add fields without DSP firmware
 * coordination.
 */
struct qda_fastrpc_msg {
	/** @remote_session_id: Session identifier on the remote processor */
	int remote_session_id;
	/** @tid: Thread ID of the invoking thread */
	int tid;
	/** @ctx: Context identifier for matching request/response */
	u64 ctx;
	/** @handle: Handle of the remote method to invoke */
	u32 handle;
	/** @sc: Scalars value encoding in/out buffer counts */
	u32 sc;
	/** @addr: Physical address of the message payload buffer */
	u64 addr;
	/** @size: Size of the message payload in bytes */
	u64 size;
} __packed;

/**
 * struct qda_msg - FastRPC message with kernel-internal bookkeeping
 */
struct qda_msg {
	/**
	 * @fastrpc: Wire-format message sent to the DSP via RPMsg.
	 * Must be the first member.
	 */
	struct qda_fastrpc_msg fastrpc;
	/** @fastrpc_ctx: Back-pointer to the owning invocation context */
	struct qda_fastrpc_invoke_ctx *fastrpc_ctx;
};

/**
 * struct qda_fastrpc_invoke_ctx - One invocation in flight
 */
struct qda_fastrpc_invoke_ctx {
	/** @qdev: Device the invocation was issued on */
	struct qda_dev *qdev;
	/** @ctxid: Wire context with the protection domain bits clear */
	u64 ctxid;
	/** @retval: Status the DSP reported, or an error if it never answered */
	int retval;
	/** @work: Completed once the DSP has answered */
	struct completion work;
	/**
	 * @refcount: Keeps the invocation alive for everyone who may still
	 * touch it: the caller and, once a message is sent, the response
	 * handler.
	 */
	struct kref refcount;
	/**
	 * @put_work: Drops the response callback's reference from process
	 * context, since the callback runs in atomic context and the final
	 * put frees DMA memory.
	 */
	struct work_struct put_work;
};

struct qda_fastrpc_invoke_ctx *qda_fastrpc_context_alloc(struct qda_dev *qdev);
void qda_fastrpc_context_free(struct kref *ref);

#endif /* __QDA_FASTRPC_H__ */
