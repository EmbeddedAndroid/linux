/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_PRIME_H__
#define __QDA_PRIME_H__

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>

struct dma_buf;
struct dma_buf_attachment;
struct qda_session;
struct sg_table;

struct drm_gem_object *qda_gem_prime_import_sg_table(struct drm_device *drm_dev,
						     struct dma_buf_attachment *attach,
						     struct sg_table *sgt);
struct drm_gem_object *qda_gem_prime_import(struct drm_device *drm_dev,
					    struct dma_buf *dma_buf);
int qda_prime_fd_to_handle(struct drm_device *drm_dev, struct drm_file *file_priv,
			   int prime_fd, u32 *handle);
struct drm_gem_object *qda_prime_import_dmabuf(struct drm_device *drm_dev,
					       struct qda_session *session,
					       struct dma_buf *dma_buf);

#endif /* __QDA_PRIME_H__ */
