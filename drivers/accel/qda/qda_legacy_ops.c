// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fcntl.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include "qda_drv.h"
#include "qda_fastrpc.h"
#include "qda_gem.h"
#include "qda_ioctl.h"
#include "qda_legacy_ops.h"
#include "qda_prime.h"

/*
 * A session the shim creates has no DRM file behind it, and so nothing
 * holding the DRM device. Take a reference here: the device is allocated
 * against the rpmsg device and would otherwise be freed on remove while a
 * /dev/fastrpc-* file is still open on it.
 */
static struct qda_session *legacy_session_create(struct qda_dev *qdev)
{
	struct qda_session *session;

	session = qda_session_alloc(qdev);
	if (!IS_ERR(session))
		drm_dev_get(&qdev->drm_dev);

	return session;
}

static void legacy_session_destroy(struct qda_session *session)
{
	struct drm_device *drm_dev = &session->qda_dev->drm_dev;

	qda_session_put(session);
	drm_dev_put(drm_dev);
}

/*
 * The ELF image of a legacy caller is named by file descriptor, so it arrives
 * already imported. An image passed as a bare pointer has no buffer to
 * resolve, so it is only accepted when there is none, i.e. an empty image.
 */
static int legacy_init_create(struct qda_session *session, struct drm_gem_object *elf_obj,
			      u32 filelen, u32 attrs, u32 siglen)
{
	struct qda_init_create_args create = {
		.init = {
			.filelen = filelen,
			.attrs = attrs,
			.siglen = siglen,
		},
		.elf_obj = elf_obj,
	};

	if (!elf_obj && filelen)
		return -EOPNOTSUPP;

	return qda_session_init_create(session, &create);
}

static int legacy_release(struct qda_session *session)
{
	/* Only a session that created a process has one to release */
	if (!READ_ONCE(session->init_mem_gem_obj))
		return 0;

	/* Unplug check only; see qda_shim_ioctl() for why not drm_dev_enter() */
	if (drm_dev_is_unplugged(&session->qda_dev->drm_dev))
		return -ENODEV;

	return qda_release_dsp_process(session->qda_dev, session);
}

static int legacy_mmap(struct qda_session *session, struct drm_gem_object *obj,
		       u64 vaddrin, u64 size, u32 flags, u64 *vaddrout)
{
	struct qda_mem_map_args args = {
		.map = {
			.request = QDA_REQUEST_MMAP,
			.flags = flags,
			.vaddrin = vaddrin,
			.size = size,
		},
		.obj = obj,
	};
	int err;

	err = qda_session_mem_map(session, &args);
	if (err)
		return err;

	*vaddrout = args.map.vaddrout;

	return 0;
}

static int legacy_mem_map(struct qda_session *session, struct drm_gem_object *obj, u32 flags,
			  u32 attrs, u32 offset, u64 vaddrin, u64 size, s32 dsp_handle,
			  u64 *vaddrout)
{
	struct qda_mem_map_args args = {
		.map = {
			.request = QDA_REQUEST_MEM_MAP,
			.flags = flags,
			.attrs = attrs,
			.offset = offset,
			.dsp_handle = dsp_handle,
			.vaddrin = vaddrin,
			.size = size,
		},
		.obj = obj,
	};
	int err;

	err = qda_session_mem_map(session, &args);
	if (err)
		return err;

	*vaddrout = args.map.vaddrout;

	return 0;
}

static int legacy_munmap(struct qda_session *session, u64 vaddrout, u64 size)
{
	struct drm_qda_mem_unmap unmap = {
		.request = QDA_REQUEST_MUNMAP,
		.vaddrout = vaddrout,
		.size = size,
	};

	return qda_session_mem_unmap(session, &unmap);
}

static int legacy_mem_unmap(struct qda_session *session, s32 dsp_handle, u64 vaddr, u64 size)
{
	struct drm_qda_mem_unmap unmap = {
		.request = QDA_REQUEST_MEM_UNMAP,
		.dsp_handle = dsp_handle,
		.vaddr = vaddr,
		.size = size,
	};

	return qda_session_mem_unmap(session, &unmap);
}

/*
 * As the GEM_CREATE ioctl, but exporting a DMA-BUF rather than installing a
 * handle: a session the shim created has no DRM file to hold handles, and the
 * legacy interface hands buffers to user space as file descriptors anyway.
 */
static struct dma_buf *legacy_alloc_dmabuf(struct qda_session *session, u64 size)
{
	struct qda_dev *qdev = session->qda_dev;
	struct drm_gem_object *gem_obj;
	struct dma_buf *dmabuf;

	gem_obj = qda_gem_create_object(&qdev->drm_dev, qdev->iommu_mgr, size, session);
	if (IS_ERR(gem_obj))
		return ERR_CAST(gem_obj);

	/* O_RDWR so the fd can be mapped writable */
	dmabuf = drm_gem_prime_export(gem_obj, O_RDWR);

	/* The DMA-BUF took its own reference if the export succeeded */
	drm_gem_object_put(gem_obj);

	return dmabuf;
}

/*
 * As the PRIME import path, without the handle: same reason as above. The
 * caller owns the returned reference.
 */
static struct drm_gem_object *legacy_import_dmabuf(struct qda_session *session,
						   struct dma_buf *dmabuf)
{
	return qda_prime_import_dmabuf(&session->qda_dev->drm_dev, session, dmabuf);
}

static int legacy_invoke(struct qda_session *session, u32 handle, u32 sc,
			 struct qda_legacy_invoke_arg *args)
{
	return qda_fastrpc_invoke_legacy(session->qda_dev, session, handle, sc, args);
}

const struct qda_legacy_ops qda_legacy_ops_table = {
	.session_create = legacy_session_create,
	.session_destroy = legacy_session_destroy,
	.init_create = legacy_init_create,
	.attach = qda_session_attach,
	.release = legacy_release,
	.mmap = legacy_mmap,
	.mem_map = legacy_mem_map,
	.munmap = legacy_munmap,
	.mem_unmap = legacy_mem_unmap,
	.alloc_dmabuf = legacy_alloc_dmabuf,
	.import_dmabuf = legacy_import_dmabuf,
	.invoke = legacy_invoke,
};
EXPORT_SYMBOL_FOR_MODULES(qda_legacy_ops_table, "qda_fastrpc_shim");
