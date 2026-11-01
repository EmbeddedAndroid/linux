// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include "qda_drv.h"
#include "qda_fastrpc.h"
#include "qda_gem.h"
#include "qda_rpmsg.h"

/*
 * The caller owns the returned reference and must drop it with
 * drm_gem_object_put().
 *
 * A handle naming a buffer mapped into another session's context bank is
 * rejected. Looking the handle up in the calling session's table is not
 * enough on its own: a buffer exported with DRM_IOCTL_PRIME_HANDLE_TO_FD and
 * imported back on the same device resolves to the original object, still
 * mapped in the exporting session's bank. The DSP derives the bank from the
 * address, so forwarding such a buffer would let one session read and write
 * in another's address space.
 */
static int get_gem_obj_from_handle(struct qda_fastrpc_invoke_ctx *ctx, u32 handle,
				   struct drm_gem_object **gem_obj)
{
	struct drm_gem_object *obj;

	if (!ctx->session->file)
		return -EOPNOTSUPP;

	obj = drm_gem_object_lookup(ctx->session->file, handle);
	if (!obj)
		return -ENOENT;

	if (to_qda_gem_obj(obj)->iommu_dev != ctx->session->assigned_iommu_dev) {
		drm_dbg_driver(&ctx->qdev->drm_dev,
			       "Buffer %u belongs to another session\n", handle);
		drm_gem_object_put(obj);
		return -EACCES;
	}

	*gem_obj = obj;

	return 0;
}

static void setup_pages_from_gem_obj(struct qda_gem_obj *qda_gem_obj,
				     struct qda_phy_page *pages)
{
	pages->addr = qda_gem_obj->dma_addr;
	pages->size = qda_gem_obj->size;
}

static int calculate_vma_offset(u64 user_ptr, u64 *offset)
{
	struct vm_area_struct *vma;
	int ret = -EFAULT;

	mmap_read_lock(current->mm);
	vma = find_vma(current->mm, user_ptr);
	if (vma && vma->vm_start <= user_ptr) {
		*offset = (user_ptr & PAGE_MASK) - vma->vm_start;
		ret = 0;
	}
	mmap_read_unlock(current->mm);

	return ret;
}

static u64 calculate_page_aligned_size(u64 addr, u64 len)
{
	u64 pg_start = (addr & PAGE_MASK) >> PAGE_SHIFT;
	u64 pg_end = ((addr + len - 1) & PAGE_MASK) >> PAGE_SHIFT;

	return (pg_end - pg_start + 1) * PAGE_SIZE;
}

/*
 * Each scalar argument is backed by exactly one contiguous DMA region: native
 * GEM objects are allocated contiguous and imported ones are rejected at
 * import time unless they are. So the metadata region needs exactly one
 * qda_remote_buf, one qda_invoke_buf and one qda_phy_page per scalar; unlike
 * drivers/misc/fastrpc.c, which supports multi-page buffers and so may need
 * several qda_phy_page entries per scalar. It ends with the handle list and
 * the CRC list.
 */
static size_t qda_fastrpc_get_meta_size(struct qda_fastrpc_invoke_ctx *ctx)
{
	return (sizeof(struct qda_remote_buf) +
		sizeof(struct qda_invoke_buf) +
		sizeof(struct qda_phy_page)) * ctx->nscalars +
		sizeof(u64) * QDA_FASTRPC_MAX_HANDLELIST +
		sizeof(u32) * QDA_FASTRPC_MAX_CRCLIST;
}

/**
 * qda_fastrpc_cleanup_handlelist() - Release DMA handles the DSP no longer needs
 * @ctx: Invocation context whose response buffer contains the handle list
 *
 * The DSP signals handle release by writing GEM handles into the handle list
 * region of the response buffer. Walk the list and close each handle, which
 * drops the GEM reference and tears down the IOMMU mapping. This must be called
 * after dma_rmb() has made the DSP's writes visible.
 *
 * The list is memory the DSP writes, so its contents are not trusted: it is
 * bounded by the region qda_fastrpc_get_meta_size() sized for it, ends at the
 * first zero entry, and each entry is looked up in the calling session's own
 * handle table, so a bad value can at worst name a buffer that session
 * already owns.
 */
void qda_fastrpc_cleanup_handlelist(struct qda_fastrpc_invoke_ctx *ctx)
{
	u64 *handlelist;
	int i;

	handlelist = (u64 *)(ctx->pages + ctx->nscalars);
	for (i = 0; i < QDA_FASTRPC_MAX_HANDLELIST; i++) {
		if (!handlelist[i])
			break;
		if (ctx->session->file)
			drm_gem_handle_delete(ctx->session->file, (u32)handlelist[i]);
	}
}

/**
 * struct qda_msg_buf_pool_entry - One idle invoke header buffer
 */
struct qda_msg_buf_pool_entry {
	/** @node: Linkage in struct qda_session.msg_buf_pool */
	struct list_head node;
	/** @gem_obj: The buffer, holding the pool's reference on it */
	struct drm_gem_object *gem_obj;
};

/*
 * Hand out one of the session's idle header buffers, so that a workload
 * invoking the same handle repeatedly does not allocate and tear down a
 * DMA-coherent, IOMMU-mapped buffer every time. The smallest buffer that
 * fits is picked, to keep a large one available for a later invocation that
 * needs it.
 *
 * Returns NULL if the pool has nothing big enough, leaving the caller to
 * allocate.
 */
static struct drm_gem_object *qda_session_msg_buf_get(struct qda_session *session, size_t size)
{
	struct qda_msg_buf_pool_entry *entry, *best = NULL;
	struct drm_gem_object *gem_obj;

	guard(mutex)(&session->msg_buf_pool_lock);

	list_for_each_entry(entry, &session->msg_buf_pool, node) {
		size_t entry_size = to_qda_gem_obj(entry->gem_obj)->size;

		if (entry_size >= size &&
		    (!best || entry_size < to_qda_gem_obj(best->gem_obj)->size))
			best = entry;
	}

	if (!best)
		return NULL;

	list_del(&best->node);
	session->msg_buf_pool_count--;
	gem_obj = best->gem_obj;
	kfree(best);

	return gem_obj;
}

/*
 * Keep a finished header buffer for the session's later invocations, or
 * release it if the session has reached its share already, so that a session
 * cannot pin an unbounded number of buffers.
 */
static void qda_session_msg_buf_put(struct qda_session *session, struct drm_gem_object *gem_obj)
{
	struct qda_msg_buf_pool_entry *entry = NULL;

	scoped_guard(mutex, &session->msg_buf_pool_lock) {
		if (session->msg_buf_pool_count < QDA_MSG_BUF_POOL_MAX)
			entry = kzalloc_obj(*entry);

		if (entry) {
			entry->gem_obj = gem_obj;
			list_add(&entry->node, &session->msg_buf_pool);
			session->msg_buf_pool_count++;
		}
	}

	if (!entry)
		drm_gem_object_put(gem_obj);
}

/**
 * qda_session_msg_buf_pool_destroy() - Release a session's pooled buffers
 * @session: Session being torn down
 *
 * Runs with the session's last reference gone, so no invocation can return a
 * buffer to the pool while it is being emptied.
 */
void qda_session_msg_buf_pool_destroy(struct qda_session *session)
{
	struct qda_msg_buf_pool_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &session->msg_buf_pool, node) {
		list_del(&entry->node);
		drm_gem_object_put(entry->gem_obj);
		kfree(entry);
	}
	session->msg_buf_pool_count = 0;
}

/**
 * qda_fastrpc_context_free() - Release an invocation
 * @ref: Reference counter embedded in the invocation
 *
 * Runs when the last reference goes. The invocation leaves the lookup table
 * here and not earlier, so that its ID cannot be handed to a new invocation
 * while this one can still be found and completed.
 *
 * The session is released last, since the buffers released before it were
 * allocated from its context bank.
 */
void qda_fastrpc_context_free(struct kref *ref)
{
	struct qda_fastrpc_invoke_ctx *ctx =
		container_of(ref, struct qda_fastrpc_invoke_ctx, refcount);
	int i;

	xa_erase(&ctx->qdev->ctx_xa, FIELD_GET(QDA_FASTRPC_CTXID_MASK, ctx->ctxid));

	if (ctx->gem_objs) {
		for (i = 0; i < ctx->nscalars; ++i) {
			if (ctx->gem_objs[i])
				drm_gem_object_put(ctx->gem_objs[i]);
		}
		kfree(ctx->gem_objs);
	}

	if (ctx->msg_gem_obj)
		qda_session_msg_buf_put(ctx->session, &ctx->msg_gem_obj->base);

	if (ctx->session)
		qda_session_put(ctx->session);

	kfree(ctx->args);
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

/*
 * Resolve the address of an in/out buffer argument. The buffer is either a
 * GEM object the legacy shim already resolved into ctx->gem_objs[i], or a GEM
 * handle looked up in the calling session's own handle table. For a handle,
 * the offset and length come from user space and are checked against the
 * buffer's real size: a region described as larger than its buffer would let
 * the DSP read and write past the end of it, into whatever else is mapped in
 * the same context bank.
 */
static int process_msg_buffer(struct qda_fastrpc_invoke_ctx *ctx, int i,
			      union qda_remote_arg *rpra,
			      struct qda_phy_page *pages)
{
	u64 len = ctx->args[i].length;
	struct drm_gem_object *gem_obj;
	struct qda_gem_obj *qda_gem_obj;
	u64 unaligned_addr;
	u64 vma_offset = 0;
	int err;

	if (ctx->gem_objs[i]) {
		/*
		 * The legacy ABI predates GEM handles, so the offset field
		 * holds an address in the caller's own mapping of the buffer,
		 * or 0 if it has none, meaning the start of the buffer.
		 */
		qda_gem_obj = to_qda_gem_obj(ctx->gem_objs[i]);
		if (ctx->args[i].offset) {
			err = calculate_vma_offset(ctx->args[i].offset, &vma_offset);
			if (err)
				return err;
		}

		rpra[i].buf.addr = ctx->args[i].offset;
		pages[i].addr = qda_gem_obj->dma_addr + vma_offset;
		pages[i].size = calculate_page_aligned_size(ctx->args[i].offset, len);

		return 0;
	}

	if (!ctx->args[i].handle)
		return -EINVAL;

	err = get_gem_obj_from_handle(ctx, ctx->args[i].handle, &gem_obj);
	if (err)
		return err;

	qda_gem_obj = to_qda_gem_obj(gem_obj);
	if (ctx->args[i].offset > qda_gem_obj->size ||
	    len > qda_gem_obj->size - ctx->args[i].offset) {
		drm_dbg_driver(&ctx->qdev->drm_dev,
			       "Argument %d is out of bounds of its buffer\n", i);
		drm_gem_object_put(gem_obj);
		return -EINVAL;
	}

	ctx->gem_objs[i] = gem_obj;

	/*
	 * The DSP takes the in-page offset from the low bits of buf.addr; the
	 * IOVA keeps those bits and stays unique per buffer, as the user VA it
	 * replaces was.
	 */
	unaligned_addr = qda_gem_obj->dma_addr + ctx->args[i].offset;
	rpra[i].buf.addr = unaligned_addr;
	pages[i].addr = unaligned_addr & PAGE_MASK;
	pages[i].size = calculate_page_aligned_size(unaligned_addr, len);

	return 0;
}

/*
 * Resolve a DMA-handle argument. The kernel needs the physical page
 * descriptor, while the DSP identifies the buffer by its GEM handle and
 * resolves that itself.
 */
static int process_dma_handle(struct qda_fastrpc_invoke_ctx *ctx, int i,
			      union qda_remote_arg *rpra,
			      struct qda_phy_page *pages)
{
	struct drm_gem_object *gem_obj;
	int err;

	if (!ctx->args[i].handle) {
		rpra[i].buf.addr = ctx->args[i].offset;
		rpra[i].buf.len = ctx->args[i].length;
		return 0;
	}

	if (ctx->gem_objs[i]) {
		gem_obj = ctx->gem_objs[i];
	} else {
		err = get_gem_obj_from_handle(ctx, ctx->args[i].handle, &gem_obj);
		if (err)
			return err;

		ctx->gem_objs[i] = gem_obj;
	}

	setup_pages_from_gem_obj(to_qda_gem_obj(gem_obj), &pages[i]);

	/* The DSP-facing offset and length fields are 32-bit by wire format */
	rpra[i].dma.handle = ctx->args[i].handle;
	rpra[i].dma.len = (u32)ctx->args[i].length;
	rpra[i].dma.offset = (u32)ctx->args[i].offset;

	return 0;
}

/**
 * qda_fastrpc_get_header_size() - Compute the FastRPC message header size
 * @ctx: FastRPC invocation context
 *
 * Two sizes come out of this: the buffer is allocated page-aligned, which is
 * what the caller gets back, while @ctx->pkt_size keeps the unaligned size
 * the DSP is told about.
 *
 * Return: Page-aligned size of the message payload buffer in bytes
 */
static size_t qda_fastrpc_get_header_size(struct qda_fastrpc_invoke_ctx *ctx)
{
	ctx->pkt_size = ALIGN(qda_fastrpc_get_meta_size(ctx), QDA_FASTRPC_ALIGN);

	return PAGE_ALIGN(ctx->pkt_size);
}

static int qda_fastrpc_get_args(struct qda_fastrpc_invoke_ctx *ctx, struct qda_msg *msg)
{
	struct qda_invoke_buf *list;
	struct qda_phy_page *pages;
	union qda_remote_arg *rpra;
	int i, err;

	msg->buf = ctx->msg_gem_obj->virt;
	msg->phys = ctx->msg_gem_obj->dma_addr;
	/*
	 * The payload is handed to the DSP whole, including the regions it
	 * writes into, so clear it rather than rely on how it was allocated.
	 */
	memset(msg->buf, 0, ctx->msg_gem_obj->size);

	/* One descriptor of each kind per argument, in three adjacent arrays */
	rpra = msg->buf;
	list = (struct qda_invoke_buf *)&rpra[ctx->nscalars];
	pages = (struct qda_phy_page *)&list[ctx->nscalars];
	/* Kept so the handle list, which follows the pages, can be found later */
	ctx->pages = pages;

	for (i = 0; i < ctx->nbufs; ++i) {
		rpra[i].buf.addr = 0;
		rpra[i].buf.len = ctx->args[i].length;
		list[i].num = ctx->args[i].length ? 1 : 0;
		list[i].pgidx = i;

		if (!ctx->args[i].length)
			continue;

		err = process_msg_buffer(ctx, i, rpra, pages);
		if (err)
			return err;
	}

	for (i = ctx->nbufs; i < ctx->nscalars; ++i) {
		list[i].num = ctx->args[i].length ? 1 : 0;
		list[i].pgidx = i;

		err = process_dma_handle(ctx, i, rpra, pages);
		if (err)
			return err;
	}

	return 0;
}

/**
 * qda_fastrpc_invoke_pack() - Pack an invocation context into a QDA message
 * @ctx: FastRPC invocation context
 * @msg: QDA message to fill in
 *
 * Allocates the payload buffer the DSP reads, resolves every argument into
 * it, and fills in the wire message that points at it.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_fastrpc_invoke_pack(struct qda_fastrpc_invoke_ctx *ctx, struct qda_msg *msg)
{
	struct qda_dev *qdev = ctx->qdev;
	struct drm_gem_object *gem_obj;
	size_t hdr_size;
	int err;

	hdr_size = qda_fastrpc_get_header_size(ctx);

	gem_obj = qda_session_msg_buf_get(ctx->session, hdr_size);
	if (!gem_obj) {
		gem_obj = qda_gem_create_object(&qdev->drm_dev, qdev->iommu_mgr,
						hdr_size, ctx->session);
		if (IS_ERR(gem_obj))
			return PTR_ERR(gem_obj);
	}

	ctx->msg_gem_obj = to_qda_gem_obj(gem_obj);

	err = qda_fastrpc_get_args(ctx, msg);
	if (err)
		return err;

	/* Ensure the payload is visible to the DSP before the doorbell */
	dma_wmb();

	msg->fastrpc.remote_session_id = ctx->session->remote_session_id;
	msg->fastrpc.tid = current->pid;
	msg->fastrpc.ctx = ctx->ctxid | QDA_USER_PD;
	msg->fastrpc.handle = ctx->handle;
	msg->fastrpc.sc = ctx->sc;
	msg->fastrpc.addr = msg->phys;
	msg->fastrpc.size = roundup(ctx->pkt_size, PAGE_SIZE);
	msg->fastrpc_ctx = ctx;

	return 0;
}

/**
 * qda_fastrpc_flush_pending() - Fail every invocation still in flight
 * @qdev: Device whose invocations should be failed
 *
 * The DSP has gone away and will never answer, so stand in for the response
 * handler: wake each waiter with an error and drop the reference the send
 * path took for that handler. Each waiter then tears its invocation down,
 * releasing the buffers it holds.
 *
 * Must run after drm_dev_unplug() has drained the response handler, so this
 * is the only path completing an invocation.
 */
void qda_fastrpc_flush_pending(struct qda_dev *qdev)
{
	struct qda_fastrpc_invoke_ctx *ctx;
	unsigned long idx;

	xa_for_each(&qdev->ctx_xa, idx, ctx) {
		ctx->retval = -EPIPE;
		complete(&ctx->work);
		kref_put(&ctx->refcount, qda_fastrpc_context_free);
	}
}

/**
 * qda_fastrpc_invoke_common() - Shared tail of a FastRPC invocation
 * @qdev: Device to invoke on
 * @ctx: Invocation whose arguments are already prepared
 *
 * Packs the invocation into a message, sends it, waits for the DSP, and acts
 * on the answer. Common to the accel UAPI and the legacy shim, once each has
 * prepared ctx->args and ctx->gem_objs by its own means.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_fastrpc_invoke_common(struct qda_dev *qdev, struct qda_fastrpc_invoke_ctx *ctx)
{
	struct qda_msg msg = {};
	int err;

	err = qda_fastrpc_invoke_pack(ctx, &msg);
	if (err)
		return err;

	err = qda_rpmsg_send_msg(qdev, &msg);
	if (err)
		return err;

	err = qda_rpmsg_wait_for_rsp(ctx);
	if (err)
		return err;

	/* Ensure the results written by the DSP are visible before reading */
	dma_rmb();

	err = ctx->retval;
	if (err) {
		drm_dbg_driver(&qdev->drm_dev, "DSP returned status %d\n", ctx->retval);
		return err;
	}

	qda_fastrpc_cleanup_handlelist(ctx);

	return 0;
}

/**
 * qda_fastrpc_invoke_legacy() - Perform a dynamic invocation on behalf of the legacy shim
 * @qdev: Device to invoke on
 * @session: Legacy session (no backing drm_file)
 * @handle: Remote handle to invoke on the DSP
 * @sc: FastRPC scalars value encoding the number of in/out buffers
 * @legacy_args: Kernel-side argument array, one entry per QDA_REMOTE_SCALARS_LENGTH(sc)
 *
 * Unlike the accel UAPI path, the argument descriptors and any backing GEM
 * objects already live in kernel memory: the shim has already resolved
 * every fd-backed argument via its own import path, since a legacy session
 * has no GEM handle table to resolve a handle-based argument against.
 * Ownership of each @legacy_args[i].obj reference transfers to this function,
 * on failure as well as on success.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_fastrpc_invoke_legacy(struct qda_dev *qdev, struct qda_session *session,
			      u32 handle, u32 sc, struct qda_legacy_invoke_arg *legacy_args)
{
	struct qda_fastrpc_invoke_ctx *ctx;
	struct drm_qda_fastrpc_invoke_args *args;
	u32 nscalars;
	int err;
	int i;

	nscalars = QDA_REMOTE_SCALARS_LENGTH(sc);

	/*
	 * An fd naming a buffer another session exported resolves to the
	 * original object, still mapped in that session's context bank, so
	 * the same check as for a handle applies.
	 */
	for (i = 0; i < nscalars; ++i) {
		if (legacy_args[i].obj &&
		    to_qda_gem_obj(legacy_args[i].obj)->iommu_dev != session->assigned_iommu_dev) {
			err = -EACCES;
			goto err_put_legacy_objs;
		}
	}

	ctx = qda_fastrpc_context_alloc(qdev);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err_put_legacy_objs;
	}

	/* Dropped by qda_fastrpc_context_free(), as for the accel UAPI path */
	qda_session_get(session);
	ctx->session = session;
	ctx->handle = handle;
	ctx->sc = sc;

	ctx->nbufs = QDA_REMOTE_SCALARS_INBUFS(sc) + QDA_REMOTE_SCALARS_OUTBUFS(sc);
	if (nscalars) {
		args = kzalloc_objs(*args, nscalars);
		if (!args) {
			err = -ENOMEM;
			goto err_put_legacy_objs_ctx;
		}

		ctx->gem_objs = kzalloc_objs(*ctx->gem_objs, nscalars);
		if (!ctx->gem_objs) {
			kfree(args);
			err = -ENOMEM;
			goto err_put_legacy_objs_ctx;
		}

		for (i = 0; i < nscalars; ++i) {
			args[i].offset = legacy_args[i].ptr;
			args[i].length = legacy_args[i].length;
			/* process_dma_handle() skips handle args whose handle is 0 */
			args[i].handle = (i >= ctx->nbufs && legacy_args[i].obj) ?
					 (u32)legacy_args[i].fd : 0;
			ctx->gem_objs[i] = legacy_args[i].obj;
		}

		ctx->args = args;
	}

	ctx->nscalars = nscalars;

	err = qda_fastrpc_invoke_common(qdev, ctx);

	kref_put(&ctx->refcount, qda_fastrpc_context_free);

	return err;

err_put_legacy_objs_ctx:
	kref_put(&ctx->refcount, qda_fastrpc_context_free);
err_put_legacy_objs:
	/* The context never took the objects, so release them here */
	for (i = 0; i < nscalars; ++i)
		if (legacy_args[i].obj)
			drm_gem_object_put(legacy_args[i].obj);

	return err;
}

/*
 * INVOKE_DYNAMIC: the argument descriptors are supplied by user space, which
 * is also responsible for having imported every buffer to a GEM handle.
 */
static int qda_fastrpc_prepare_args_invoke(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct drm_qda_invoke_args *invoke_args = argp;
	struct drm_qda_fastrpc_invoke_args *args;
	u32 nscalars;

	ctx->handle = invoke_args->handle;
	ctx->sc = invoke_args->sc;

	nscalars = QDA_REMOTE_SCALARS_LENGTH(ctx->sc);
	if (!nscalars)
		return 0;

	args = kzalloc_objs(*args, nscalars);
	if (!args)
		return -ENOMEM;

	if (copy_from_user(args, u64_to_user_ptr(invoke_args->args),
			   nscalars * sizeof(*args))) {
		kfree(args);
		return -EFAULT;
	}

	ctx->args = args;

	return 0;
}

/**
 * qda_fastrpc_prepare_args() - Prepare arguments for a FastRPC invocation
 * @ctx: FastRPC invocation context
 * @session: Session issuing the invocation
 * @argp: Kernel-side ioctl argument buffer owned by the DRM core
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_fastrpc_prepare_args(struct qda_fastrpc_invoke_ctx *ctx,
			     struct qda_session *session, void *argp)
{
	int err;

	/*
	 * Held until the invocation is freed, which may be after the file the
	 * session was opened on is closed: the DSP owns the payload buffer
	 * until it answers, and that buffer came from the session.
	 */
	qda_session_get(session);
	ctx->session = session;

	err = qda_fastrpc_prepare_args_invoke(ctx, argp);
	if (err)
		return err;

	ctx->nscalars = QDA_REMOTE_SCALARS_LENGTH(ctx->sc);
	ctx->nbufs = QDA_REMOTE_SCALARS_INBUFS(ctx->sc) + QDA_REMOTE_SCALARS_OUTBUFS(ctx->sc);

	if (ctx->nscalars && !ctx->gem_objs) {
		ctx->gem_objs = kzalloc_objs(*ctx->gem_objs, ctx->nscalars);
		if (!ctx->gem_objs)
			return -ENOMEM;
	}

	return 0;
}
