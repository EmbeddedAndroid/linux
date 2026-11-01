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
#include <drm/qda_accel.h>

struct drm_gem_object;
struct qda_dev;
struct qda_gem_obj;
struct qda_session;

/*
 * FastRPC scalar extraction macros
 *
 * These macros extract different fields from the scalar value that describes
 * the arguments passed in a FastRPC invocation.
 */
#define QDA_REMOTE_SCALARS_INBUFS(sc)		(((sc) >> 16) & 0x0ff)
#define QDA_REMOTE_SCALARS_OUTBUFS(sc)		(((sc) >> 8) & 0x0ff)
#define QDA_REMOTE_SCALARS_INHANDLES(sc)	(((sc) >> 4) & 0x0f)
#define QDA_REMOTE_SCALARS_OUTHANDLES(sc)	((sc) & 0x0f)
#define QDA_REMOTE_SCALARS_LENGTH(sc)		(QDA_REMOTE_SCALARS_INBUFS(sc) +    \
						 QDA_REMOTE_SCALARS_OUTBUFS(sc) +   \
						 QDA_REMOTE_SCALARS_INHANDLES(sc) + \
						 QDA_REMOTE_SCALARS_OUTHANDLES(sc))

/* FastRPC configuration constants */
#define QDA_FASTRPC_ALIGN		128	/* Alignment the DSP expects of the payload */
#define QDA_FASTRPC_MAX_HANDLELIST	16	/* Handles the DSP can ask to release */
#define QDA_FASTRPC_MAX_CRCLIST		64	/* CRC list entries */

/* Protection domain a session's invocations run in */
#define QDA_USER_PD			1

/* Idle invoke header buffers a session keeps for reuse */
#define QDA_MSG_BUF_POOL_MAX		8

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
 * struct qda_remote_dmahandle - Remote DMA handle descriptor
 */
struct qda_remote_dmahandle {
	/** @handle: Handle of the buffer */
	s32 handle;
	/** @offset: Byte offset within the buffer object */
	u32 offset;
	/** @len: Length of the region in bytes */
	u32 len;
};

/**
 * struct qda_remote_buf - Remote buffer descriptor
 */
struct qda_remote_buf {
	/** @addr: Address of the argument as the DSP sees it */
	u64 addr;
	/** @len: Length of the buffer in bytes */
	u64 len;
};

/**
 * union qda_remote_arg - Remote argument (buffer or DMA handle)
 */
union qda_remote_arg {
	/** @buf: Buffer descriptor */
	struct qda_remote_buf buf;
	/** @dma: DMA handle descriptor */
	struct qda_remote_dmahandle dma;
};

/**
 * struct qda_phy_page - Physical page descriptor
 */
struct qda_phy_page {
	/** @addr: Physical (IOMMU) address of the region */
	u64 addr;
	/** @size: Size of the contiguous region in bytes */
	u64 size;
};

/**
 * struct qda_invoke_buf - Invoke buffer descriptor
 */
struct qda_invoke_buf {
	/** @num: Number of contiguous physical regions */
	u32 num;
	/** @pgidx: Index into the physical page array */
	u32 pgidx;
};

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
	/** @buf: Kernel virtual address of the payload buffer */
	void *buf;
	/** @phys: Physical/DMA address of the payload buffer */
	u64 phys;
	/** @fastrpc_ctx: Back-pointer to the owning invocation context */
	struct qda_fastrpc_invoke_ctx *fastrpc_ctx;
};

/**
 * struct qda_fastrpc_invoke_ctx - One invocation in flight
 */
struct qda_fastrpc_invoke_ctx {
	/** @qdev: Device the invocation was issued on */
	struct qda_dev *qdev;
	/** @session: Session that initiated this invocation */
	struct qda_session *session;
	/** @ctxid: Wire context with the protection domain bits clear */
	u64 ctxid;
	/** @retval: Status the DSP reported, or an error if it never answered */
	int retval;
	/** @nscalars: Total number of scalar arguments */
	int nscalars;
	/** @nbufs: Number of buffer arguments; the rest are handle arguments */
	int nbufs;
	/** @handle: Handle of the remote method being invoked */
	u32 handle;
	/** @sc: Scalars value encoding in/out buffer counts */
	u32 sc;
	/** @pkt_size: Total payload size in bytes */
	size_t pkt_size;
	/** @args: Invoke argument descriptors */
	struct drm_qda_fastrpc_invoke_args *args;
	/** @gem_objs: Buffer each argument resolved to, held until the end */
	struct drm_gem_object **gem_objs;
	/** @msg_gem_obj: GEM object backing the message payload buffer */
	struct qda_gem_obj *msg_gem_obj;
	/** @pages: Array of physical page descriptors inside the payload */
	struct qda_phy_page *pages;
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
void qda_fastrpc_cleanup_handlelist(struct qda_fastrpc_invoke_ctx *ctx);
void qda_session_msg_buf_pool_destroy(struct qda_session *session);
int qda_fastrpc_prepare_args(struct qda_fastrpc_invoke_ctx *ctx,
			     struct qda_session *session, void *argp);
int qda_fastrpc_invoke_pack(struct qda_fastrpc_invoke_ctx *ctx, struct qda_msg *msg);

#endif /* __QDA_FASTRPC_H__ */
