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

/* Encodes a method ID and the in/out buffer counts; handle counts are zero */
#define QDA_FASTRPC_SCALARS(method, in, out)	((((method) & 0x1f) << 24) | \
						 (((in) & 0xff) << 16) |     \
						 (((out) & 0xff) << 8))

/* FastRPC configuration constants */
#define QDA_FASTRPC_ALIGN		128	/* Alignment the DSP expects of the payload */
#define QDA_FASTRPC_MAX_HANDLELIST	16	/* Handles the DSP can ask to release */
#define QDA_FASTRPC_MAX_CRCLIST		64	/* CRC list entries */

/* Methods of the initialisation handle */
#define QDA_FASTRPC_RMID_INIT_ATTACH		0
#define QDA_FASTRPC_RMID_INIT_RELEASE		1
#define QDA_FASTRPC_RMID_INIT_MMAP		4
#define QDA_FASTRPC_RMID_INIT_MUNMAP		5
#define QDA_FASTRPC_RMID_INIT_CREATE		6
#define QDA_FASTRPC_RMID_INIT_CREATE_ATTR	7
#define QDA_FASTRPC_RMID_INIT_CREATE_STATIC	8
#define QDA_FASTRPC_RMID_INIT_MEM_MAP		10
#define QDA_FASTRPC_RMID_INIT_MEM_UNMAP		11
#define QDA_FASTRPC_RMID_INVOKE_DYNAMIC		0xFFFFFFFFU

/*
 * ctx->type tag for the DSP capability query. Not a wire method ID: the query
 * is addressed by QDA_FASTRPC_DSP_UTILITIES_HANDLE, method 0.
 */
#define QDA_FASTRPC_TYPE_GET_DSP_INFO		0xFFFFFFFEU

/* Handle the initialisation methods are addressed to */
#define QDA_FASTRPC_INIT_HANDLE		0x1

/* Handle of the DSP's utility calls, which include the capability query */
#define QDA_FASTRPC_DSP_UTILITIES_HANDLE	0x2

/* Status the DSP returns when it does not support the capability query */
#define QDA_FASTRPC_DSP_UNSUPPORTED_API		0x80000414U

/* Protection domains a request can be directed at */
#define QDA_ROOT_PD			0
#define QDA_USER_PD			1
#define QDA_SENSORS_PD			2

/* Largest ELF image a process may be created from */
#define QDA_FASTRPC_INIT_FILELEN_MAX	(4 * 1024 * 1024)

/* Longest static PD name, including the NUL */
#define QDA_FASTRPC_INIT_NAMELEN_MAX	128

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
 * struct qda_fastrpc_create_process_inbuf - Input buffer for process creation
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_create_process_inbuf {
	/** @remote_session_id: Session the process belongs to */
	int remote_session_id;
	/** @namelen: Length of the process name including the NUL */
	u32 namelen;
	/** @filelen: Length of the ELF image in bytes */
	u32 filelen;
	/** @pageslen: Number of physical page descriptors */
	u32 pageslen;
	/** @attrs: Process attribute flags */
	u32 attrs;
	/** @siglen: Length of the signature data in bytes */
	u32 siglen;
};

/**
 * struct qda_fastrpc_create_static_inbuf - Input buffer for attaching to a static PD
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_create_static_inbuf {
	/** @remote_session_id: Session the attach belongs to */
	int remote_session_id;
	/** @namelen: Length of the static PD name including the NUL */
	u32 namelen;
	/** @pageslen: Number of remote heap descriptors that follow the name (0 or 1) */
	u32 pageslen;
};

/**
 * struct qda_fastrpc_map_req_msg - Request to map a whole buffer
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_map_req_msg {
	/** @remote_session_id: Session the mapping belongs to */
	s32 remote_session_id;
	/** @flags: Mapping flags (cache attributes, permissions) */
	u32 flags;
	/** @vaddr: Address the DSP should map the buffer at, if it can */
	u64 vaddr;
	/** @num: Size of the physical page descriptor in bytes */
	s32 num;
};

/**
 * struct qda_fastrpc_mem_map_req_msg - Request to map a region with attributes
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_mem_map_req_msg {
	/** @remote_session_id: Session the mapping belongs to */
	s32 remote_session_id;
	/** @handle: Identifier the DSP tracks the buffer by */
	s32 handle;
	/** @offset: Offset within the buffer */
	s32 offset;
	/** @flags: Mapping flags (cache attributes, permissions) */
	u32 flags;
	/** @vaddrin: Address the DSP should map the buffer at, if it can */
	u64 vaddrin;
	/** @num: Size of the physical page descriptor in bytes */
	s32 num;
	/** @data_len: Length of additional inline data */
	s32 data_len;
};

/**
 * struct qda_fastrpc_map_rsp_msg - The DSP's answer to a map request
 */
struct qda_fastrpc_map_rsp_msg {
	/** @vaddrout: Address the DSP mapped the buffer at */
	u64 vaddrout;
};

/**
 * struct qda_fastrpc_munmap_req_msg - Request to remove a plain mapping
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_munmap_req_msg {
	/** @remote_session_id: Session the mapping belongs to */
	s32 remote_session_id;
	/** @vaddr: Address the mapping was made at */
	u64 vaddr;
	/** @size: Length of the mapping in bytes */
	u64 size;
};

/**
 * struct qda_fastrpc_mem_unmap_req_msg - Request to remove an attribute-based mapping
 *
 * Layout the DSP expects; do not reorder.
 */
struct qda_fastrpc_mem_unmap_req_msg {
	/** @remote_session_id: Session the mapping belongs to */
	s32 remote_session_id;
	/** @handle: Identifier the DSP tracks the buffer by */
	s32 handle;
	/** @vaddrin: Address the mapping was made at */
	u64 vaddrin;
	/** @len: Length of the mapping in bytes */
	u64 len;
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
	/** @pd: Protection domain, OR'ed into the low bits of @ctxid when packed */
	int pd;
	/** @type: Method being invoked (QDA_FASTRPC_RMID_*) */
	u32 type;
	/** @legacy: Arguments came from the legacy shim, where offsets are user VAs */
	bool legacy;
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
	/** @init_mem_gem_obj: Memory the DSP loads a new process into */
	struct qda_gem_obj *init_mem_gem_obj;
	/** @map_obj: Buffer being mapped into the DSP, held until the request is done */
	struct drm_gem_object *map_obj;
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

/**
 * struct qda_legacy_invoke_arg - Kernel-side invoke argument for the legacy shim
 *
 * Built by the FastRPC compatibility shim from a legacy fastrpc_invoke_args
 * entry; @obj is NULL for an argument with no backing fd.
 */
struct qda_legacy_invoke_arg {
	/** @ptr: Argument pointer (user virtual address) */
	u64 ptr;
	/** @length: Length of the argument data in bytes */
	u64 length;
	/** @obj: Pre-resolved GEM object for an fd-backed argument, or NULL */
	struct drm_gem_object *obj;
	/** @fd: Legacy fd, forwarded as the DSP-visible handle for handle arguments */
	s32 fd;
};

/**
 * struct qda_init_create_args - Kernel-side arguments for creating a DSP process
 *
 * Wraps the UAPI arguments so that a caller with no handle table, such as the
 * legacy shim, can pass the ELF image as an already imported buffer.
 */
struct qda_init_create_args {
	/** @init: Creation parameters; @init.file must be zero */
	struct drm_qda_init_create init;
	/**
	 * @elf_obj: Buffer holding the ELF image, mapped in the session's
	 * context bank, or NULL if @init.filehandle names it. Borrowed: the
	 * caller keeps its reference for the length of the call.
	 */
	struct drm_gem_object *elf_obj;
};

/**
 * struct qda_mem_map_args - Kernel-side arguments for mapping a buffer into the DSP
 *
 * Wraps the UAPI arguments so that a caller with no handle table, such as the
 * legacy shim, can pass an already imported buffer.
 */
struct qda_mem_map_args {
	/** @map: Mapping parameters; @map.vaddrout is filled in on success */
	struct drm_qda_mem_map map;
	/**
	 * @obj: Buffer to map, in the session's context bank, or NULL if
	 * @map.handle names it. Borrowed: the caller keeps its reference for
	 * the length of the call.
	 */
	struct drm_gem_object *obj;
};

/**
 * struct qda_static_pd_args - Kernel-side arguments for attaching to a static PD
 */
struct qda_static_pd_args {
	/** @name: Kernel copy of the NUL-terminated PD name */
	const char *name;
	/** @namelen: Length of @name including the NUL */
	u32 namelen;
	/** @heap: Remote heap to hand the DSP, or zero size if it already has it */
	struct qda_phy_page heap;
};

struct qda_fastrpc_invoke_ctx *qda_fastrpc_context_alloc(struct qda_dev *qdev);
void qda_fastrpc_context_free(struct kref *ref);
void qda_fastrpc_cleanup_handlelist(struct qda_fastrpc_invoke_ctx *ctx);
void qda_session_msg_buf_pool_destroy(struct qda_session *session);
int qda_fastrpc_prepare_args(struct qda_fastrpc_invoke_ctx *ctx,
			     struct qda_session *session, void *argp);
int qda_fastrpc_invoke_pack(struct qda_fastrpc_invoke_ctx *ctx, struct qda_msg *msg);
void qda_fastrpc_return_result(struct qda_fastrpc_invoke_ctx *ctx, void *argp);
int qda_fastrpc_invoke_common(struct qda_dev *qdev, struct qda_fastrpc_invoke_ctx *ctx);
void qda_fastrpc_flush_pending(struct qda_dev *qdev);
int qda_fastrpc_invoke_legacy(struct qda_dev *qdev, struct qda_session *session,
			      u32 handle, u32 sc, struct qda_legacy_invoke_arg *legacy_args);

#endif /* __QDA_FASTRPC_H__ */
