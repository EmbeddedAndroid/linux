/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_IOCTL_H__
#define __QDA_IOCTL_H__

#include "qda_drv.h"

struct drm_qda_mem_unmap;
struct qda_init_create_args;
struct qda_mem_map_args;

int qda_ioctl_query(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_gem_create(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_gem_mmap_offset(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_invoke(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_init_create(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_mmap(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_munmap(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_ioctl_attach(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qda_session_init_create(struct qda_session *session, struct qda_init_create_args *create);
int qda_session_attach(struct qda_session *session, u32 pd_type, const char __user *name,
		       u32 namelen);
int qda_session_mem_map(struct qda_session *session, struct qda_mem_map_args *args);
int qda_session_mem_unmap(struct qda_session *session, struct drm_qda_mem_unmap *unmap);

#endif /* __QDA_IOCTL_H__ */
