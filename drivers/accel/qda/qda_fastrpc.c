// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/bitfield.h>
#include <linux/capability.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
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
 * several qda_phy_page entries per scalar. It ends with the handle list, the
 * CRC list and the poll word.
 */
static size_t qda_fastrpc_get_meta_size(struct qda_fastrpc_invoke_ctx *ctx)
{
	return (sizeof(struct qda_remote_buf) +
		sizeof(struct qda_invoke_buf) +
		sizeof(struct qda_phy_page)) * ctx->nscalars +
		sizeof(u64) * QDA_FASTRPC_MAX_HANDLELIST +
		sizeof(u32) * QDA_FASTRPC_MAX_CRCLIST +
		sizeof(u32);
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

/*
 * The DSP reaches a buffer it has mapped until it is unmapped, whatever user
 * space does with its handle, so the session takes over the request's
 * reference once the DSP has accepted the mapping: the DMA mapping must
 * outlive every DSP access to it. Called on success, and from the context's
 * release if the DSP accepted a mapping whose caller stopped waiting.
 */
static void qda_fastrpc_keep_map(struct qda_fastrpc_invoke_ctx *ctx)
{
	struct qda_fastrpc_map_rsp_msg *rsp;
	int idx = ctx->type == QDA_FASTRPC_RMID_INIT_MMAP ? 2 : 3;

	if (!ctx->map || !ctx->map_obj)
		return;

	/* The DSP wrote the response before it signalled completion */
	dma_rmb();
	rsp = (struct qda_fastrpc_map_rsp_msg *)
	      ((u8 *)to_qda_gem_obj(ctx->gem_objs[idx])->virt + ctx->args[idx].offset);
	ctx->map->obj = ctx->map_obj;
	ctx->map->vaddr = rsp->vaddrout;
	ctx->map_obj = NULL;

	scoped_guard(mutex, &ctx->session->maps_lock)
		list_add_tail(&ctx->map->node, &ctx->session->maps);
	ctx->map = NULL;
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

	if (READ_ONCE(ctx->is_work_done) && !ctx->retval)
		qda_fastrpc_keep_map(ctx);

	if (ctx->gem_objs) {
		for (i = 0; i < ctx->nscalars; ++i) {
			if (ctx->gem_objs[i])
				drm_gem_object_put(ctx->gem_objs[i]);
		}
		kfree(ctx->gem_objs);
	}

	if (ctx->msg_gem_obj)
		qda_session_msg_buf_put(ctx->session, &ctx->msg_gem_obj->base);

	if (ctx->init_mem_gem_obj)
		drm_gem_object_put(&ctx->init_mem_gem_obj->base);

	if (ctx->map_obj)
		drm_gem_object_put(ctx->map_obj);

	kfree(ctx->map);

	if (ctx->inline_obj)
		drm_gem_object_put(&ctx->inline_obj->base);

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
 * Allocate one DMA-coherent object to hold every buffer the driver builds for
 * one of its own requests, so they cost a single page rather than one each.
 * The memory is cleared, so output slots start out clean.
 */
static struct qda_gem_obj *alloc_kernel_gem(struct qda_fastrpc_invoke_ctx *ctx, size_t size)
{
	struct qda_dev *qdev = ctx->qdev;
	struct drm_gem_object *gem_obj;

	gem_obj = qda_gem_create_object(&qdev->drm_dev, qdev->iommu_mgr, size, ctx->session);
	if (IS_ERR(gem_obj))
		return ERR_CAST(gem_obj);

	memset(to_qda_gem_obj(gem_obj)->virt, 0, to_qda_gem_obj(gem_obj)->size);

	return to_qda_gem_obj(gem_obj);
}

/*
 * The context releases whatever has been allocated here if a later step
 * fails, which needs @nscalars to cover every slot that may hold a reference.
 */
static int alloc_kernel_args(struct qda_fastrpc_invoke_ctx *ctx, u32 nargs)
{
	ctx->args = kzalloc_objs(*ctx->args, nargs);
	if (!ctx->args)
		return -ENOMEM;

	ctx->gem_objs = kzalloc_objs(*ctx->gem_objs, nargs);
	if (!ctx->gem_objs)
		return -ENOMEM;

	ctx->nscalars = nargs;

	return 0;
}

/* Describe the region at @ptr in @kernel_gem as argument @idx, taking a reference on it */
static void assign_kernel_gem_arg(struct qda_fastrpc_invoke_ctx *ctx, int idx,
				  struct qda_gem_obj *kernel_gem, void *ptr, size_t len)
{
	ctx->args[idx].offset = (u8 *)ptr - (u8 *)kernel_gem->virt;
	ctx->args[idx].length = len;
	drm_gem_object_get(&kernel_gem->base);
	ctx->gem_objs[idx] = &kernel_gem->base;
}

/*
 * Resolve the address of an in/out buffer argument. The buffer is either a
 * GEM object already resolved into ctx->gem_objs[i], by the legacy shim or by
 * the driver for its own requests, or a GEM handle looked up in the calling
 * session's own handle table. For a handle,
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

	if (ctx->gem_objs[i] && ctx->legacy &&
	    (!ctx->inline_obj || ctx->gem_objs[i] != &ctx->inline_obj->base)) {
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

	if (ctx->gem_objs[i]) {
		/* Built by the driver, so the offset is a byte offset into the buffer */
		qda_gem_obj = to_qda_gem_obj(ctx->gem_objs[i]);
		unaligned_addr = qda_gem_obj->dma_addr + ctx->args[i].offset;
		rpra[i].buf.addr = unaligned_addr;
		pages[i].addr = unaligned_addr & PAGE_MASK;
		pages[i].size = calculate_page_aligned_size(unaligned_addr, len);

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
	/* The poll word follows the handle list and the CRC list */
	ctx->poll_addr = (u32 *)((u8 *)(pages + ctx->nscalars) +
				 sizeof(u64) * QDA_FASTRPC_MAX_HANDLELIST +
				 sizeof(u32) * QDA_FASTRPC_MAX_CRCLIST);

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

	/* The session has no process yet to identify while one is being created */
	if (ctx->handle == QDA_FASTRPC_INIT_HANDLE)
		msg->fastrpc.remote_session_id = 0;
	else
		msg->fastrpc.remote_session_id = ctx->session->remote_session_id;

	msg->fastrpc.tid = current->pid;
	msg->fastrpc.ctx = ctx->ctxid | ctx->pd;
	msg->fastrpc.handle = ctx->handle;
	msg->fastrpc.sc = ctx->sc;
	msg->fastrpc.addr = msg->phys;
	msg->fastrpc.size = roundup(ctx->pkt_size, PAGE_SIZE);
	msg->fastrpc_ctx = ctx;

	return 0;
}

static u32 qda_fastrpc_read_poll_addr(struct qda_fastrpc_invoke_ctx *ctx)
{
	dma_rmb();

	return READ_ONCE(*ctx->poll_addr);
}

/*
 * Busy-wait for the DSP to write QDA_FASTRPC_POLL_RESPONSE into the poll word,
 * which saves the interrupt and scheduling latency of waiting for the rpmsg
 * response. The response still arrives afterwards and drops the reference
 * qda_rpmsg_send_msg() took for it as usual.
 *
 * Return: 0 if the invocation completed, negative error code if it has not
 * answered within QDA_FASTRPC_POLL_MAX_TIMEOUT_US
 */
static int qda_fastrpc_poll_for_rsp(struct qda_fastrpc_invoke_ctx *ctx)
{
	u32 val;
	int err;

	err = read_poll_timeout_atomic(qda_fastrpc_read_poll_addr, val,
				       val == QDA_FASTRPC_POLL_RESPONSE ||
				       READ_ONCE(ctx->is_work_done),
				       1, QDA_FASTRPC_POLL_MAX_TIMEOUT_US, false, ctx);
	if (err)
		return err;

	/* The rpmsg path records its own status, so only the poll path's is set here */
	if (val == QDA_FASTRPC_POLL_RESPONSE) {
		WRITE_ONCE(ctx->is_work_done, true);
		ctx->retval = 0;
	}

	return 0;
}

static int qda_fastrpc_wait_for_completion(struct qda_fastrpc_invoke_ctx *ctx)
{
	if (ctx->is_polled && !qda_fastrpc_poll_for_rsp(ctx))
		return 0;

	/* Polling timed out, so fall back to waiting for the rpmsg response */
	ctx->is_polled = false;

	return qda_rpmsg_wait_for_rsp(ctx);
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
		WRITE_ONCE(ctx->is_work_done, true);
		complete(&ctx->work);
		kref_put(&ctx->refcount, qda_fastrpc_context_free);
	}
}

/**
 * qda_fastrpc_return_result() - Copy what a request returns into the ioctl data
 * @ctx: Invocation that completed successfully
 * @argp: Kernel-side ioctl argument buffer owned by the DRM core
 *
 * Buffer arguments are written by the DSP straight into memory the caller can
 * see, so only the mapping requests have a value to surface, which the DSP
 * wrote into the response slot the driver built for it.
 */
void qda_fastrpc_return_result(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct qda_mem_map_args *map_args = argp;
	struct qda_fastrpc_map_rsp_msg *rsp;
	int idx;

	switch (ctx->type) {
	case QDA_FASTRPC_TYPE_GET_DSP_INFO:
		/* @argp is the attribute array; entry 0 is reserved */
		((u32 *)argp)[0] = 0;
		memcpy((u32 *)argp + 1,
		       (u8 *)to_qda_gem_obj(ctx->gem_objs[1])->virt + ctx->args[1].offset,
		       ctx->args[1].length);
		return;
	case QDA_FASTRPC_RMID_INIT_MMAP:
		idx = 2;
		break;
	case QDA_FASTRPC_RMID_INIT_MEM_MAP:
		idx = 3;
		break;
	default:
		return;
	}

	rsp = (struct qda_fastrpc_map_rsp_msg *)
	      ((u8 *)to_qda_gem_obj(ctx->gem_objs[idx])->virt + ctx->args[idx].offset);
	map_args->map.vaddrout = rsp->vaddrout;

	qda_fastrpc_keep_map(ctx);
}

/**
 * qda_session_maps_forget() - Drop the record of a mapping the DSP removed
 * @session: Session the mapping belongs to
 * @mem_unmap: true for an INIT_MEM_UNMAP request, matched by fd and address,
 *             false for INIT_MUNMAP, matched by address and covering the
 *             whole mapping (size 0 meaning all of it)
 * @unmap: The request the DSP accepted
 */
void qda_session_maps_forget(struct qda_session *session, bool mem_unmap,
			     const struct drm_qda_mem_unmap *unmap)
{
	struct qda_session_map *m, *found = NULL;

	scoped_guard(mutex, &session->maps_lock) {
		list_for_each_entry(m, &session->maps, node) {
			if (mem_unmap ? m->fd == unmap->dsp_handle && m->vaddr == unmap->vaddr :
					m->vaddr == unmap->vaddrout &&
					(!unmap->size || unmap->size >= m->size)) {
				found = m;
				list_del(&m->node);
				break;
			}
		}
	}

	if (!found)
		return;

	drm_gem_object_put(found->obj);
	kfree(found);
}

/**
 * qda_session_maps_destroy() - Release every mapping a session still holds
 * @session: Session being released, whose DSP process is already gone
 */
void qda_session_maps_destroy(struct qda_session *session)
{
	struct qda_session_map *m, *tmp;

	list_for_each_entry_safe(m, tmp, &session->maps, node) {
		list_del(&m->node);
		drm_gem_object_put(m->obj);
		kfree(m);
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

	/* Only invocations of a dynamically loaded module in a user PD are polled */
	ctx->is_polled = READ_ONCE(ctx->session->poll_mode) && ctx->pd == QDA_USER_PD &&
			 ctx->handle > QDA_FASTRPC_MAX_STATIC_HANDLE;

	err = qda_rpmsg_send_msg(qdev, &msg);
	if (err)
		return err;

	err = qda_fastrpc_wait_for_completion(ctx);
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

/*
 * A legacy buffer argument with no DMA-BUF (fd -1 or 0) points at the
 * caller's own memory, which the DSP cannot reach. As drivers/misc/fastrpc.c
 * does, copy every such argument into one driver buffer of the session
 * before the invocation, and the output arguments back after it.
 */
static int qda_fastrpc_legacy_inline_in(struct qda_fastrpc_invoke_ctx *ctx,
					struct qda_legacy_invoke_arg *legacy_args)
{
	struct qda_gem_obj *buf;
	u64 size = 0;
	int i;

	/* Each length is at most SZ_1G, so a u64 sum over every argument cannot wrap */
	for (i = 0; i < ctx->nbufs; ++i) {
		if (legacy_args[i].obj || !legacy_args[i].length)
			continue;
		if (legacy_args[i].length > SZ_1G)
			return -EINVAL;
		size += ALIGN(legacy_args[i].length, QDA_FASTRPC_ALIGN);
	}
	if (size > SZ_1G)
		return -EINVAL;

	if (!size)
		return 0;

	buf = alloc_kernel_gem(ctx, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	ctx->inline_obj = buf;

	size = 0;
	for (i = 0; i < ctx->nbufs; ++i) {
		if (legacy_args[i].obj || !legacy_args[i].length)
			continue;

		drm_gem_object_get(&buf->base);
		ctx->gem_objs[i] = &buf->base;
		ctx->args[i].offset = size;

		/*
		 * Output arguments too: one that overlaps an input then starts
		 * with the input's bytes, as with fastrpc.c's shared region
		 */
		if (copy_from_user((u8 *)buf->virt + size, u64_to_user_ptr(legacy_args[i].ptr),
				   legacy_args[i].length))
			return -EFAULT;

		size += ALIGN(legacy_args[i].length, QDA_FASTRPC_ALIGN);
	}

	return 0;
}

static int qda_fastrpc_legacy_inline_out(struct qda_fastrpc_invoke_ctx *ctx,
					 struct qda_legacy_invoke_arg *legacy_args)
{
	u32 inbufs = QDA_REMOTE_SCALARS_INBUFS(ctx->sc);
	int i;

	if (!ctx->inline_obj)
		return 0;

	for (i = inbufs; i < ctx->nbufs; ++i) {
		if (ctx->gem_objs[i] != &ctx->inline_obj->base)
			continue;

		if (copy_to_user(u64_to_user_ptr(legacy_args[i].ptr),
				 (u8 *)ctx->inline_obj->virt + ctx->args[i].offset,
				 legacy_args[i].length))
			return -EFAULT;
	}

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
	ctx->type = QDA_FASTRPC_RMID_INVOKE_DYNAMIC;
	ctx->legacy = true;
	ctx->pd = READ_ONCE(session->pd);
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

	err = qda_fastrpc_legacy_inline_in(ctx, legacy_args);
	if (err)
		goto out_put;

	err = qda_fastrpc_invoke_common(qdev, ctx);
	if (!err)
		err = qda_fastrpc_legacy_inline_out(ctx, legacy_args);

out_put:
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
 * Use the ELF buffer the caller imported itself as argument 2. It is checked
 * as a buffer named by handle would be: it must be mapped in this session's
 * context bank, and the image must fit in it, as the DSP would otherwise read
 * past the end of the buffer.
 */
static int qda_fastrpc_use_elf_obj(struct qda_fastrpc_invoke_ctx *ctx,
				   struct drm_gem_object *obj, u32 filelen)
{
	if (to_qda_gem_obj(obj)->iommu_dev != ctx->session->assigned_iommu_dev)
		return -EACCES;

	if (filelen > to_qda_gem_obj(obj)->size)
		return -EINVAL;

	drm_gem_object_get(obj);
	ctx->gem_objs[2] = obj;

	return 0;
}

/*
 * INIT_RELEASE and INIT_ATTACH packed layout:
 *   [0] remote_session_id	sizeof(int)	IN
 */
static int qda_fastrpc_prepare_args_session(struct qda_fastrpc_invoke_ctx *ctx, u32 rmid)
{
	struct qda_gem_obj *kernel_gem;
	int err;

	ctx->sc = QDA_FASTRPC_SCALARS(rmid, 1, 0);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 1);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, sizeof(int));
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);

	*(int *)kernel_gem->virt = ctx->session->remote_session_id;
	assign_kernel_gem_arg(ctx, 0, kernel_gem, kernel_gem->virt, sizeof(int));
	drm_gem_object_put(&kernel_gem->base);

	return 0;
}

/*
 * INIT_CREATE / INIT_CREATE_ATTR packed layout:
 *   [0] inbuf	sizeof(qda_fastrpc_create_process_inbuf)	IN
 *   [1] comm	TASK_COMM_LEN					IN
 *   [2] ELF	buffer named by the caller's handle, not packed	IN
 *   [3] pages	sizeof(qda_phy_page), the process image memory	IN
 *
 * The attributes and signature length travel in @inbuf. The _ATTR method is
 * used when any attribute is set, as in drivers/misc/fastrpc.c.
 */
static int qda_fastrpc_prepare_args_init_create(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct qda_init_create_args *create = argp;
	struct drm_qda_init_create *init = &create->init;
	struct qda_fastrpc_create_process_inbuf *inbuf;
	struct qda_dev *qdev = ctx->qdev;
	struct drm_gem_object *gem_obj;
	struct qda_gem_obj *kernel_gem;
	u8 *gv;
	int err;

	const size_t off_inbuf = 0;
	const size_t off_comm = off_inbuf + sizeof(*inbuf);
	const size_t off_pages = off_comm + TASK_COMM_LEN;
	const size_t total_size = off_pages + sizeof(struct qda_phy_page);

	if (init->file || init->filelen > QDA_FASTRPC_INIT_FILELEN_MAX)
		return -EINVAL;

	/* A signed process runs with the DSP's trust in the signature */
	if (!(init->attrs & QDA_INIT_ATTR_UNSIGNED_MODULE) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* The DSP loads the image into this memory and keeps using it while the process runs */
	gem_obj = qda_gem_create_object(&qdev->drm_dev, qdev->iommu_mgr,
					QDA_FASTRPC_INIT_FILELEN_MAX, ctx->session);
	if (IS_ERR(gem_obj))
		return PTR_ERR(gem_obj);

	ctx->init_mem_gem_obj = to_qda_gem_obj(gem_obj);

	ctx->sc = init->attrs ? QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_CREATE_ATTR, 4, 0)
			      : QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_CREATE, 4, 0);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 4);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, total_size);
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);
	gv = kernel_gem->virt;

	inbuf = (struct qda_fastrpc_create_process_inbuf *)(gv + off_inbuf);
	inbuf->remote_session_id = ctx->session->remote_session_id;
	inbuf->filelen = init->filelen;
	inbuf->pageslen = 1;
	inbuf->attrs = init->attrs;
	inbuf->siglen = init->siglen;

	strscpy(gv + off_comm, current->comm, TASK_COMM_LEN);
	inbuf->namelen = strlen(gv + off_comm) + 1;

	setup_pages_from_gem_obj(ctx->init_mem_gem_obj, (struct qda_phy_page *)(gv + off_pages));

	assign_kernel_gem_arg(ctx, 0, kernel_gem, inbuf, sizeof(*inbuf));
	assign_kernel_gem_arg(ctx, 1, kernel_gem, gv + off_comm, inbuf->namelen);
	/* The image starts at the beginning of the buffer the caller named */
	ctx->args[2].length = init->filelen;
	if (create->elf_obj) {
		err = qda_fastrpc_use_elf_obj(ctx, create->elf_obj, init->filelen);
		if (err)
			goto out_put;
	} else {
		ctx->args[2].handle = init->filehandle;
	}
	assign_kernel_gem_arg(ctx, 3, kernel_gem, gv + off_pages, sizeof(struct qda_phy_page));

out_put:
	drm_gem_object_put(&kernel_gem->base);

	return err;
}

/*
 * INIT_CREATE_STATIC packed layout:
 *   [0] inbuf	sizeof(qda_fastrpc_create_static_inbuf)	IN
 *   [1] name	namelen, including the NUL		IN
 *   [2] heap	sizeof(qda_phy_page)			IN
 *
 * The heap descriptor is always present; its size is zero, and so pageslen,
 * when the DSP already has the remote heap.
 */
static int qda_fastrpc_prepare_args_create_static(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct qda_static_pd_args *sargs = argp;
	struct qda_fastrpc_create_static_inbuf *inbuf;
	struct qda_gem_obj *kernel_gem;
	u8 *gv;
	int err;

	const size_t off_inbuf = 0;
	const size_t off_name = off_inbuf + sizeof(*inbuf);
	const size_t off_heap = ALIGN(off_name + sargs->namelen, sizeof(u64));
	const size_t total_size = off_heap + sizeof(struct qda_phy_page);

	ctx->sc = QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_CREATE_STATIC, 3, 0);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 3);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, total_size);
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);
	gv = kernel_gem->virt;

	inbuf = (struct qda_fastrpc_create_static_inbuf *)(gv + off_inbuf);
	inbuf->remote_session_id = ctx->session->remote_session_id;
	inbuf->namelen = sargs->namelen;
	inbuf->pageslen = sargs->heap.size ? 1 : 0;

	memcpy(gv + off_name, sargs->name, sargs->namelen);
	memcpy(gv + off_heap, &sargs->heap, sizeof(sargs->heap));

	assign_kernel_gem_arg(ctx, 0, kernel_gem, gv + off_inbuf, sizeof(*inbuf));
	assign_kernel_gem_arg(ctx, 1, kernel_gem, gv + off_name, sargs->namelen);
	assign_kernel_gem_arg(ctx, 2, kernel_gem, gv + off_heap, sizeof(struct qda_phy_page));
	drm_gem_object_put(&kernel_gem->base);

	return 0;
}

/*
 * DSP capability query packed layout:
 *   [0] count	sizeof(u32), the capacity of the array below	IN
 *   [1] attrs	(QDA_MAX_DSP_ATTRIBUTES - 1) * sizeof(u32)	OUT
 *
 * Index 0 of the cached array is reserved, so only the rest is exchanged.
 */
static int qda_fastrpc_prepare_args_get_dsp_info(struct qda_fastrpc_invoke_ctx *ctx)
{
	const u32 attr_count = QDA_MAX_DSP_ATTRIBUTES - 1;
	struct qda_gem_obj *kernel_gem;
	int err;

	const size_t off_attrs = sizeof(u32);
	const size_t total_size = off_attrs + attr_count * sizeof(u32);

	ctx->sc = QDA_FASTRPC_SCALARS(0, 1, 1);
	ctx->handle = QDA_FASTRPC_DSP_UTILITIES_HANDLE;

	err = alloc_kernel_args(ctx, 2);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, total_size);
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);

	*(u32 *)kernel_gem->virt = attr_count;

	assign_kernel_gem_arg(ctx, 0, kernel_gem, kernel_gem->virt, sizeof(u32));
	assign_kernel_gem_arg(ctx, 1, kernel_gem, (u8 *)kernel_gem->virt + off_attrs,
			      attr_count * sizeof(u32));
	drm_gem_object_put(&kernel_gem->base);

	return 0;
}

/*
 * Find the buffer a mapping request names and describe it to the DSP. The
 * buffer is held by the context until the request is done, so that closing its
 * handle meanwhile cannot free memory the DSP is being told to map. The
 * region must fit in the buffer, as the DSP would otherwise map past its end.
 *
 * A caller with no handle table supplies the buffer already imported. It is
 * checked as a buffer named by handle would be: an fd can name a buffer that
 * another session exported, which resolves to the original object, still
 * mapped in that session's context bank.
 */
static int setup_mmap_pages(struct qda_fastrpc_invoke_ctx *ctx, struct qda_mem_map_args *args,
			    struct qda_phy_page *pages)
{
	struct drm_qda_mem_map *margs = &args->map;
	struct qda_gem_obj *qda_gem_obj;
	int err;

	if (args->obj) {
		if (to_qda_gem_obj(args->obj)->iommu_dev != ctx->session->assigned_iommu_dev)
			return -EACCES;

		drm_gem_object_get(args->obj);
		ctx->map_obj = args->obj;
	} else {
		if (!margs->handle)
			return -EINVAL;

		err = get_gem_obj_from_handle(ctx, margs->handle, &ctx->map_obj);
		if (err)
			return err;
	}

	qda_gem_obj = to_qda_gem_obj(ctx->map_obj);
	if (margs->size > qda_gem_obj->size)
		return -EINVAL;

	ctx->map = kzalloc_obj(*ctx->map);
	if (!ctx->map)
		return -ENOMEM;

	pages->addr = qda_gem_obj->dma_addr;
	pages->size = margs->size ? margs->size : qda_gem_obj->size;
	ctx->map->fd = margs->dsp_handle;
	ctx->map->size = pages->size;

	return 0;
}

/*
 * INIT_MMAP packed layout:
 *   [0] req	sizeof(qda_fastrpc_map_req_msg)		IN
 *   [1] pages	sizeof(qda_phy_page)			IN
 *   [2] rsp	sizeof(qda_fastrpc_map_rsp_msg)		OUT
 */
static int qda_fastrpc_prepare_args_map(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct qda_mem_map_args *map_args = argp;
	struct drm_qda_mem_map *margs = &map_args->map;
	struct qda_fastrpc_map_req_msg *req;
	struct qda_gem_obj *kernel_gem;
	u8 *gv;
	int err;

	const size_t off_req = 0;
	const size_t off_pages = off_req + sizeof(*req);
	const size_t off_rsp = off_pages + sizeof(struct qda_phy_page);
	const size_t total_size = off_rsp + sizeof(struct qda_fastrpc_map_rsp_msg);

	ctx->sc = QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_MMAP, 2, 1);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 3);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, total_size);
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);
	gv = kernel_gem->virt;

	req = (struct qda_fastrpc_map_req_msg *)(gv + off_req);
	req->remote_session_id = ctx->session->remote_session_id;
	req->flags = margs->flags;
	req->vaddr = margs->vaddrin;
	req->num = sizeof(struct qda_phy_page);

	err = setup_mmap_pages(ctx, map_args, (struct qda_phy_page *)(gv + off_pages));
	if (err)
		goto out_put;

	assign_kernel_gem_arg(ctx, 0, kernel_gem, gv + off_req, sizeof(*req));
	assign_kernel_gem_arg(ctx, 1, kernel_gem, gv + off_pages, sizeof(struct qda_phy_page));
	assign_kernel_gem_arg(ctx, 2, kernel_gem, gv + off_rsp,
			      sizeof(struct qda_fastrpc_map_rsp_msg));

out_put:
	drm_gem_object_put(&kernel_gem->base);

	return err;
}

/*
 * INIT_MEM_MAP packed layout:
 *   [0] req	sizeof(qda_fastrpc_mem_map_req_msg)	IN
 *   [1] pages	sizeof(qda_phy_page)			IN
 *   [2] empty	0 bytes, which the DSP requires		IN
 *   [3] rsp	sizeof(qda_fastrpc_map_rsp_msg)		OUT
 */
static int qda_fastrpc_prepare_args_mem_map(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct qda_mem_map_args *map_args = argp;
	struct drm_qda_mem_map *margs = &map_args->map;
	struct qda_fastrpc_mem_map_req_msg *req;
	struct qda_gem_obj *kernel_gem;
	u8 *gv;
	int err;

	const size_t off_req = 0;
	const size_t off_pages = off_req + sizeof(*req);
	const size_t off_rsp = off_pages + sizeof(struct qda_phy_page);
	const size_t total_size = off_rsp + sizeof(struct qda_fastrpc_map_rsp_msg);

	/* The wire field is signed */
	if (margs->offset > INT_MAX)
		return -EINVAL;

	ctx->sc = QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_MEM_MAP, 3, 1);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 4);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, total_size);
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);
	gv = kernel_gem->virt;

	req = (struct qda_fastrpc_mem_map_req_msg *)(gv + off_req);
	req->remote_session_id = ctx->session->remote_session_id;
	req->handle = margs->dsp_handle;
	req->offset = margs->offset;
	req->flags = margs->flags;
	req->vaddrin = margs->vaddrin;
	req->num = sizeof(struct qda_phy_page);
	req->data_len = 0;

	err = setup_mmap_pages(ctx, map_args, (struct qda_phy_page *)(gv + off_pages));
	if (err)
		goto out_put;

	assign_kernel_gem_arg(ctx, 0, kernel_gem, gv + off_req, sizeof(*req));
	assign_kernel_gem_arg(ctx, 1, kernel_gem, gv + off_pages, sizeof(struct qda_phy_page));
	assign_kernel_gem_arg(ctx, 2, kernel_gem, gv + off_pages, 0);
	assign_kernel_gem_arg(ctx, 3, kernel_gem, gv + off_rsp,
			      sizeof(struct qda_fastrpc_map_rsp_msg));

out_put:
	drm_gem_object_put(&kernel_gem->base);

	return err;
}

/*
 * INIT_MUNMAP packed layout:
 *   [0] req	sizeof(qda_fastrpc_munmap_req_msg)	IN
 */
static int qda_fastrpc_prepare_args_munmap(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct drm_qda_mem_unmap *uargs = argp;
	struct qda_fastrpc_munmap_req_msg *req;
	struct qda_gem_obj *kernel_gem;
	int err;

	ctx->sc = QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_MUNMAP, 1, 0);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 1);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, sizeof(*req));
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);

	req = kernel_gem->virt;
	req->remote_session_id = ctx->session->remote_session_id;
	req->vaddr = uargs->vaddrout;
	req->size = uargs->size;

	assign_kernel_gem_arg(ctx, 0, kernel_gem, req, sizeof(*req));
	drm_gem_object_put(&kernel_gem->base);

	return 0;
}

/*
 * INIT_MEM_UNMAP packed layout:
 *   [0] req	sizeof(qda_fastrpc_mem_unmap_req_msg)	IN
 */
static int qda_fastrpc_prepare_args_mem_unmap(struct qda_fastrpc_invoke_ctx *ctx, void *argp)
{
	struct drm_qda_mem_unmap *uargs = argp;
	struct qda_fastrpc_mem_unmap_req_msg *req;
	struct qda_gem_obj *kernel_gem;
	int err;

	ctx->sc = QDA_FASTRPC_SCALARS(QDA_FASTRPC_RMID_INIT_MEM_UNMAP, 1, 0);
	ctx->handle = QDA_FASTRPC_INIT_HANDLE;

	err = alloc_kernel_args(ctx, 1);
	if (err)
		return err;

	kernel_gem = alloc_kernel_gem(ctx, sizeof(*req));
	if (IS_ERR(kernel_gem))
		return PTR_ERR(kernel_gem);

	req = kernel_gem->virt;
	req->remote_session_id = ctx->session->remote_session_id;
	req->handle = uargs->dsp_handle;
	req->vaddrin = uargs->vaddr;
	req->len = uargs->size;

	assign_kernel_gem_arg(ctx, 0, kernel_gem, req, sizeof(*req));
	drm_gem_object_put(&kernel_gem->base);

	return 0;
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
	ctx->pd = READ_ONCE(session->pd);

	switch (ctx->type) {
	case QDA_FASTRPC_RMID_INIT_RELEASE:
		err = qda_fastrpc_prepare_args_session(ctx, QDA_FASTRPC_RMID_INIT_RELEASE);
		break;
	case QDA_FASTRPC_RMID_INIT_ATTACH:
		/* The DSP tells the domains apart by the one the request is directed at */
		ctx->pd = *(int *)argp;
		err = qda_fastrpc_prepare_args_session(ctx, QDA_FASTRPC_RMID_INIT_ATTACH);
		break;
	case QDA_FASTRPC_RMID_INIT_CREATE:
		/* The new process is in the user PD, and so is the request creating it */
		ctx->pd = QDA_USER_PD;
		err = qda_fastrpc_prepare_args_init_create(ctx, argp);
		break;
	case QDA_FASTRPC_RMID_INIT_CREATE_STATIC:
		ctx->pd = QDA_USER_PD;
		err = qda_fastrpc_prepare_args_create_static(ctx, argp);
		break;
	case QDA_FASTRPC_TYPE_GET_DSP_INFO:
		err = qda_fastrpc_prepare_args_get_dsp_info(ctx);
		break;
	case QDA_FASTRPC_RMID_INIT_MMAP:
		err = qda_fastrpc_prepare_args_map(ctx, argp);
		break;
	case QDA_FASTRPC_RMID_INIT_MEM_MAP:
		err = qda_fastrpc_prepare_args_mem_map(ctx, argp);
		break;
	case QDA_FASTRPC_RMID_INIT_MUNMAP:
		err = qda_fastrpc_prepare_args_munmap(ctx, argp);
		break;
	case QDA_FASTRPC_RMID_INIT_MEM_UNMAP:
		err = qda_fastrpc_prepare_args_mem_unmap(ctx, argp);
		break;
	case QDA_FASTRPC_RMID_INVOKE_DYNAMIC:
		err = qda_fastrpc_prepare_args_invoke(ctx, argp);
		break;
	default:
		return -EINVAL;
	}
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
