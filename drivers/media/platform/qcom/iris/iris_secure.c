// SPDX-License-Identifier: GPL-2.0-only
/*
 * Secure sessions: the codec works only on protected memory.
 *
 * Every buffer of a secure session is a protected DMA-buf: the queues take
 * them from user space, the internal buffers come from the protected video
 * heap. The codec reaches them through its secure context banks, one
 * device per secure-* subnode, on which the IOMMU maps only protected
 * memory (the mapping of anything else fails). The firmware is told that
 * the session is secure and which context bank each buffer is in.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/iommu-prot.h>
#include <linux/module.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "iris_buffer.h"
#include "iris_core.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_instance.h"
#include "iris_resources.h"
#include "iris_secure.h"

MODULE_IMPORT_NS("DMA_BUF_HEAP");

/* Firmware limit */
#define IRIS_SECURE_MAX_SESSIONS	3

/* The heap of the TEE's secure video memory */
#define IRIS_SECURE_HEAP		"protected,secure-video"

bool iris_secure_supported(struct iris_core *core)
{
	struct dma_heap *heap;
	int i;

	if (!IS_ENABLED(CONFIG_DMABUF_HEAPS) ||
	    !core->iris_firmware_data->secure_sessions)
		return false;
	for (i = 0; i < IRIS_SECURE_CB_MAX; i++)
		if (!core->sec_dev[i] || !iommu_prot_device(core->sec_dev[i]))
			return false;
	heap = dma_heap_find(IRIS_SECURE_HEAP);
	if (!heap)
		return false;
	dma_heap_put(heap);
	return true;
}

/* Context bank of a buffer in a secure session, as the firmware expects */
static int iris_secure_cb(struct iris_inst *inst, enum iris_buffer_type type)
{
	switch (type) {
	case BUF_INPUT:
		return inst->domain == DECODER ? IRIS_SECURE_CB_BITSTREAM :
						 IRIS_SECURE_CB_PIXEL;
	case BUF_OUTPUT:
		return inst->domain == DECODER ? IRIS_SECURE_CB_PIXEL :
						 IRIS_SECURE_CB_BITSTREAM;
	case BUF_DPB:
	case BUF_PARTIAL:
	case BUF_SCRATCH_1:
	case BUF_SCRATCH_2:
	case BUF_VPSS:
		return IRIS_SECURE_CB_PIXEL;
	case BUF_BIN:
		return IRIS_SECURE_CB_BITSTREAM;
	case BUF_ARP:
	case BUF_COMV:
	case BUF_LINE:
	case BUF_NON_COMV:
	case BUF_PERSIST:
		return IRIS_SECURE_CB_NON_PIXEL;
	default:
		return -EINVAL;
	}
}

static u32 iris_secure_hfi_flags(int cb)
{
	switch (cb) {
	case IRIS_SECURE_CB_BITSTREAM:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_BITSTREAM;
	case IRIS_SECURE_CB_PIXEL:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_PIXEL;
	default:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_NON_PIXEL;
	}
}

static struct device *iris_secure_dev(struct iris_inst *inst,
				      enum iris_buffer_type type)
{
	int cb = iris_secure_cb(inst, type);

	return cb < 0 ? NULL : inst->core->sec_dev[cb];
}

/* A V4L2 buffer of a secure session, already mapped on its context bank */
int iris_secure_buf_init(struct iris_inst *inst, struct vb2_buffer *vb2,
			 struct iris_buffer *buf)
{
	int cb = iris_secure_cb(inst, buf->type);

	if (cb < 0)
		return cb;

	buf->device_addr = vb2_dma_contig_plane_dma_addr(vb2, 0);
	buf->hfi_cb_flags = iris_secure_hfi_flags(cb);
	return 0;
}

int iris_secure_alloc_internal(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct device *dev = iris_secure_dev(inst, buf->type);
	struct dma_buf_attachment *att;
	struct dma_heap *heap;
	struct sg_table *sgt;
	struct dma_buf *dbuf;
	int ret;

	if (!IS_ENABLED(CONFIG_DMABUF_HEAPS))
		return -ENODEV;
	if (!dev)
		return -EINVAL;
	heap = dma_heap_find(IRIS_SECURE_HEAP);
	if (!heap)
		return -ENODEV;

	dbuf = dma_heap_buffer_alloc(heap, buf->buffer_size, O_RDWR, 0);
	dma_heap_put(heap);
	if (IS_ERR(dbuf)) {
		dev_err(dev, "protected internal buffer type %d of %#zx: %ld\n",
			buf->type, buf->buffer_size, PTR_ERR(dbuf));
		return PTR_ERR(dbuf);
	}

	att = dma_buf_attach(dbuf, dev);
	if (IS_ERR(att)) {
		ret = PTR_ERR(att);
		goto err_put;
	}
	sgt = dma_buf_map_attachment_unlocked(att, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_detach;
	}
	/* The firmware takes one device address per buffer */
	if (sgt->nents != 1) {
		ret = -EINVAL;
		goto err_unmap;
	}

	buf->device_addr = sg_dma_address(sgt->sgl);
	buf->hfi_cb_flags = iris_secure_hfi_flags(iris_secure_cb(inst, buf->type));
	buf->sec_dbuf = dbuf;
	buf->sec_att = att;
	buf->sec_sgt = sgt;
	return 0;

err_unmap:
	dma_buf_unmap_attachment_unlocked(att, sgt, DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(dbuf, att);
err_put:
	dma_buf_put(dbuf);
	return ret;
}

void iris_secure_free_internal(struct iris_inst *inst, struct iris_buffer *buf)
{
	dma_buf_unmap_attachment_unlocked(buf->sec_att, buf->sec_sgt,
					  DMA_BIDIRECTIONAL);
	dma_buf_detach(buf->sec_dbuf, buf->sec_att);
	dma_buf_put(buf->sec_dbuf);
	buf->sec_dbuf = NULL;
}

/*
 * Switch an instance to secure before its queues have buffers: both queues
 * then take DMA-bufs only, mapped on the secure context banks.
 */
int iris_secure_set(struct iris_inst *inst, bool secure)
{
	struct iris_core *core = inst->core;
	struct vb2_queue *src, *dst;
	struct iris_inst *other;
	int n = 0, ret = 0;

	if (secure == inst->secure)
		return 0;
	if (secure && !iris_secure_supported(core))
		return -EOPNOTSUPP;

	src = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	dst = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	/*
	 * The queue lock keeps REQBUFS out until the queues are switched. It
	 * is only tried: the control handler lock is held here, and taking
	 * the queue lock after it could invert the order.
	 */
	if (!mutex_trylock(&inst->ctx_q_lock))
		return -EBUSY;
	/* The firmware session keeps its kind once its properties are set */
	if (vb2_is_busy(src) || vb2_is_busy(dst) || inst->once_per_session_set) {
		ret = -EBUSY;
		goto out;
	}

	mutex_lock(&core->lock);
	if (secure) {
		list_for_each_entry(other, &core->instances, list)
			n += other->secure;
		if (n >= IRIS_SECURE_MAX_SESSIONS) {
			mutex_unlock(&core->lock);
			ret = -EBUSY;
			goto out;
		}
	}
	inst->secure = secure;
	mutex_unlock(&core->lock);

	src->io_modes = secure ? VB2_DMABUF : VB2_MMAP | VB2_DMABUF;
	dst->io_modes = src->io_modes;
	src->dev = secure ? iris_secure_dev(inst, BUF_INPUT) :
			    iris_get_cb_dev(inst, BUF_INPUT);
	dst->dev = secure ? iris_secure_dev(inst, BUF_OUTPUT) :
			    iris_get_cb_dev(inst, BUF_OUTPUT);
out:
	mutex_unlock(&inst->ctx_q_lock);
	return ret;
}
