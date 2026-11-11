// SPDX-License-Identifier: GPL-2.0-only
/*
 * Secure sessions: the codec works only on protected memory.
 *
 * A secure session takes protected DMA-bufs from the TEE protected heaps
 * on both queues and allocates its internal buffers from the same heap.
 * That memory is lent to the secure world, so the host cannot read it,
 * and the host IOMMU cannot map it for the codec either. The codec reaches
 * it through its secure context banks, which the hypervisor owns: the
 * driver gives the hypervisor the physical range of each buffer and an
 * IOVA in the bank's window, and the hypervisor maps it only if the
 * memory is lent. The firmware is told the session is secure and which
 * bank each buffer is in.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/genalloc.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/tee_core.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "../../../../iommu/arm/arm-smmu/arm-smmu-pkvm.h"

#include "iris_buffer.h"
#include "iris_core.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_instance.h"
#include "iris_resources.h"
#include "iris_secure.h"

#define IRIS_SECURE_MAX_SESSIONS	3

/*
 * Test hook: skip the driver's own heap checks, so that a test can show
 * the hypervisor refusing unlent memory in a secure bank and the SMMU
 * refusing a non-secure session's DMA to protected memory.
 */
static bool secure_test_skip_heap_check;
module_param(secure_test_skip_heap_check, bool, 0644);
MODULE_PARM_DESC(secure_test_skip_heap_check,
		 "Test only: let the hypervisor and the SMMU refuse buffers of the wrong kind");

static bool iris_secure_supported(struct iris_core *core)
{
	return core->sec_dev && core->sec_iova[ARM_SMMU_PKVM_BANK_NON_PIXEL];
}

/* Context bank of a buffer in a secure session (downstream routing) */
static int iris_secure_bank(struct iris_inst *inst, enum iris_buffer_type type)
{
	switch (type) {
	case BUF_INPUT:
		return inst->domain == DECODER ? ARM_SMMU_PKVM_BANK_BITSTREAM :
						 ARM_SMMU_PKVM_BANK_PIXEL;
	case BUF_OUTPUT:
		return inst->domain == DECODER ? ARM_SMMU_PKVM_BANK_PIXEL :
						 ARM_SMMU_PKVM_BANK_BITSTREAM;
	case BUF_DPB:
	case BUF_PARTIAL:
	case BUF_SCRATCH_1:
	case BUF_SCRATCH_2:
	case BUF_VPSS:
		return ARM_SMMU_PKVM_BANK_PIXEL;
	case BUF_BIN:
		return ARM_SMMU_PKVM_BANK_BITSTREAM;
	case BUF_ARP:
	case BUF_COMV:
	case BUF_LINE:
	case BUF_NON_COMV:
	case BUF_PERSIST:
		return ARM_SMMU_PKVM_BANK_NON_PIXEL;
	default:
		return -EINVAL;
	}
}

static u32 iris_secure_hfi_flags(int bank)
{
	switch (bank) {
	case ARM_SMMU_PKVM_BANK_BITSTREAM:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_BITSTREAM;
	case ARM_SMMU_PKVM_BANK_PIXEL:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_PIXEL;
	default:
		return HFI_BUF_HOST_FLAGS_CB_SECURE_NON_PIXEL;
	}
}

static int iris_secure_map(struct iris_inst *inst, struct iris_buffer *buf,
			   phys_addr_t phys, size_t size)
{
	struct iris_core *core = inst->core;
	int bank = iris_secure_bank(inst, buf->type);
	unsigned long iova;
	int ret;

	if (bank < 0)
		return bank;

	size = PAGE_ALIGN(size);
	iova = gen_pool_alloc(core->sec_iova[bank], size);
	if (!iova) {
		dev_err(core->dev, "secure bank %d IOVA space full for %#zx (%zu free)\n",
			bank, size, gen_pool_avail(core->sec_iova[bank]));
		return -ENOSPC;
	}

	ret = arm_smmu_pkvm_prot_map(bank, iova, phys, size);
	if (ret) {
		dev_err(core->dev, "secure bank %d refused %pa+%#zx: %d\n",
			bank, &phys, size, ret);
		gen_pool_free(core->sec_iova[bank], iova, size);
		return ret;
	}

	buf->sec_bank = bank;
	buf->sec_iova = iova;
	buf->sec_size = size;
	buf->device_addr = iova;
	buf->hfi_cb_flags = iris_secure_hfi_flags(bank);
	return 0;
}

static void iris_secure_unmap(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct iris_core *core = inst->core;

	if (!buf->sec_size)
		return;

	WARN_ON(arm_smmu_pkvm_prot_unmap(buf->sec_bank, buf->sec_iova,
					 buf->sec_size));
	gen_pool_free(core->sec_iova[buf->sec_bank], buf->sec_iova, buf->sec_size);
	buf->sec_size = 0;
}

/*
 * A V4L2 buffer of a secure session: the DMA-buf must be protected, its
 * mapping on the secure device gives its physical range.
 */
int iris_secure_buf_init(struct iris_inst *inst, struct vb2_buffer *vb2,
			 struct iris_buffer *buf)
{
	struct dma_buf *dbuf = vb2->planes[0].dbuf;

	if (!dbuf || (!secure_test_skip_heap_check && tee_heap_dma_buf_id(dbuf) < 0))
		return -EINVAL;

	return iris_secure_map(inst, buf, vb2_dma_contig_plane_dma_addr(vb2, 0),
			       vb2_plane_size(vb2, 0));
}

void iris_secure_buf_cleanup(struct iris_inst *inst, struct iris_buffer *buf)
{
	iris_secure_unmap(inst, buf);
}

/* A protected DMA-buf in a non-secure session is refused as well */
bool iris_secure_buf_refused(struct iris_inst *inst, struct vb2_buffer *vb2)
{
	struct dma_buf *dbuf = vb2->planes[0].dbuf;

	return !inst->secure && dbuf && !secure_test_skip_heap_check &&
	       tee_heap_dma_buf_id(dbuf) >= 0;
}

int iris_secure_alloc_internal(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct iris_core *core = inst->core;
	struct dma_buf_attachment *att;
	struct sg_table *sgt;
	struct dma_buf *dbuf;
	int ret;

	dbuf = tee_heap_alloc(TEE_DMA_HEAP_SECURE_VIDEO_PLAY,
			      PAGE_ALIGN(buf->buffer_size));
	if (IS_ERR(dbuf)) {
		dev_err(core->dev, "protected internal buffer type %d of %#zx: %ld\n",
			buf->type, buf->buffer_size, PTR_ERR(dbuf));
		return PTR_ERR(dbuf);
	}

	att = dma_buf_attach(dbuf, core->sec_dev);
	if (IS_ERR(att)) {
		ret = PTR_ERR(att);
		goto err_put;
	}
	sgt = dma_buf_map_attachment_unlocked(att, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_detach;
	}
	/* The protected heap hands out physically contiguous buffers */
	if (sgt->nents != 1) {
		dev_err(core->dev, "protected internal buffer in %u pieces\n", sgt->nents);
		ret = -EINVAL;
		goto err_unmap;
	}

	ret = iris_secure_map(inst, buf, sg_dma_address(sgt->sgl),
			      sg_dma_len(sgt->sgl));
	if (ret)
		goto err_unmap;
	dev_dbg(core->dev, "protected internal buffer type %d of %#zx at bank %d iova %#x\n",
		buf->type, buf->buffer_size, buf->sec_bank, buf->sec_iova);

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
	iris_secure_unmap(inst, buf);
	dma_buf_unmap_attachment_unlocked(buf->sec_att, buf->sec_sgt,
					  DMA_BIDIRECTIONAL);
	dma_buf_detach(buf->sec_dbuf, buf->sec_att);
	dma_buf_put(buf->sec_dbuf);
	buf->sec_dbuf = NULL;
}

/*
 * Switch an instance to secure before its queues have buffers: both queues
 * take DMA-bufs only, mapped on the secure device (no IOMMU, physical
 * addresses), never CPU-synchronised.
 */
int iris_secure_set(struct iris_inst *inst, bool secure)
{
	struct iris_core *core = inst->core;
	struct vb2_queue *src, *dst;
	struct iris_inst *other;
	int n = 0;

	if (secure == inst->secure)
		return 0;
	if (secure && !iris_secure_supported(core))
		return -EOPNOTSUPP;

	src = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	dst = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	if (vb2_is_busy(src) || vb2_is_busy(dst))
		return -EBUSY;

	if (secure) {
		mutex_lock(&core->lock);
		list_for_each_entry(other, &core->instances, list)
			n += other->secure;
		mutex_unlock(&core->lock);
		if (n >= IRIS_SECURE_MAX_SESSIONS)
			return -EBUSY;
	}

	inst->secure = secure;
	src->io_modes = secure ? VB2_DMABUF : VB2_MMAP | VB2_DMABUF;
	dst->io_modes = src->io_modes;
	src->dev = secure ? core->sec_dev : iris_get_cb_dev(inst, BUF_INPUT);
	dst->dev = secure ? core->sec_dev : iris_get_cb_dev(inst, BUF_OUTPUT);
	return 0;
}

static const char * const iris_secure_pool_names[] = {
	"iris-secure-bitstream", "iris-secure-pixel", "iris-secure-non-pixel",
};

int iris_secure_init(struct iris_core *core)
{
	struct platform_device_info info = {
		.name		= "iris-secure",
		.id		= PLATFORM_DEVID_AUTO,
		.parent		= core->dev,
		.dma_mask	= DMA_BIT_MASK(64),
	};
	struct platform_device *pdev;
	u32 start, end;
	int b, ret;

	if (!IS_ENABLED(CONFIG_ARM_SMMU_PKVM) ||
	    !arm_smmu_pkvm_prot_bank(ARM_SMMU_PKVM_BANK_NON_PIXEL, &start, &end))
		return 0;

	for (b = 0; b < ARRAY_SIZE(core->sec_iova); b++) {
		struct gen_pool *pool;

		if (!arm_smmu_pkvm_prot_bank(b, &start, &end))
			goto err;
		/* one pool per bank: a device holds a single unnamed pool */
		pool = devm_gen_pool_create(core->dev, PAGE_SHIFT, -1, iris_secure_pool_names[b]);
		if (IS_ERR(pool))
			goto err;
		core->sec_iova[b] = pool;
		ret = gen_pool_add(core->sec_iova[b], start, end - start, -1);
		if (ret)
			goto err;
	}

	/* No IOMMU: the mapping of a protected DMA-buf is its physical range */
	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev))
		goto err;
	dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	core->sec_dev = &pdev->dev;
	dev_info(core->dev, "secure sessions through the hypervisor secure banks\n");
	return 0;

err:
	memset(core->sec_iova, 0, sizeof(core->sec_iova));
	dev_warn(core->dev, "no secure sessions\n");
	return 0;
}

void iris_secure_deinit(struct iris_core *core)
{
	if (core->sec_dev)
		platform_device_unregister(to_platform_device(core->sec_dev));
	core->sec_dev = NULL;
}
