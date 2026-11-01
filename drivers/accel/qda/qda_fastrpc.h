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
};

struct qda_fastrpc_invoke_ctx *qda_fastrpc_context_alloc(struct qda_dev *qdev);
void qda_fastrpc_context_free(struct kref *ref);

#endif /* __QDA_FASTRPC_H__ */
