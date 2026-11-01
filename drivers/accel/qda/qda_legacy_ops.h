/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_LEGACY_OPS_H__
#define __QDA_LEGACY_OPS_H__

#include <linux/types.h>

struct dma_buf;
struct drm_gem_object;
struct qda_dev;
struct qda_legacy_invoke_arg;
struct qda_session;

/**
 * struct qda_legacy_ops - What the legacy FastRPC shim may call
 *
 * The shim serves the /dev/fastrpc-* interface of the driver this one
 * replaces, on top of the same session and buffer handling the accel UAPI
 * uses. It is a separate module, so the entry points it needs are collected
 * here and exported as one table: that way the driver does not depend on the
 * shim, and neither module has to reimplement what the other does.
 *
 * Ops are added as the shim grows to need them.
 */
struct qda_legacy_ops {
	/** @session_create: Open a session on @qdev */
	struct qda_session *(*session_create)(struct qda_dev *qdev);
	/** @session_destroy: Close a session */
	void (*session_destroy)(struct qda_session *session);
	/** @init_create: Create a DSP process from an imported ELF image */
	int (*init_create)(struct qda_session *session, struct drm_gem_object *elf_obj,
			   u32 filelen, u32 attrs, u32 siglen);
	/** @release: Release the session's DSP process, if it has one */
	int (*release)(struct qda_session *session);
	/** @alloc_dmabuf: Allocate a buffer and export it as a DMA-BUF */
	struct dma_buf *(*alloc_dmabuf)(struct qda_session *session, u64 size);
	/** @import_dmabuf: Map a DMA-BUF into a session's context bank */
	struct drm_gem_object *(*import_dmabuf)(struct qda_session *session,
						struct dma_buf *dmabuf);
	/** @invoke: Run a method on the DSP; takes ownership of each @args[i].obj */
	int (*invoke)(struct qda_session *session, u32 handle, u32 sc,
		      struct qda_legacy_invoke_arg *args);
};

extern const struct qda_legacy_ops qda_legacy_ops_table;

#endif /* __QDA_LEGACY_OPS_H__ */
