/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_ACCEL_H__
#define __QDA_ACCEL_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_QDA_QUERY		0x00

#define DRM_IOCTL_QDA_QUERY		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_QUERY, \
					 struct drm_qda_query)

/* Query type definitions for drm_qda_query */
#define QDA_QUERY_DSP_NAME		1

/**
 * struct drm_qda_query - Device information query structure
 * @query_type: Type of query (input)
 * @dsp_name: Null-terminated name of the DSP (returned when query_type is
 *            QDA_QUERY_DSP_NAME)
 *
 * This structure is used with DRM_IOCTL_QDA_QUERY to query device attributes
 * based on @query_type.
 */
struct drm_qda_query {
	__u32 query_type;
	__u8 dsp_name[16];
};

#if defined(__cplusplus)
}
#endif

#endif /* __QDA_ACCEL_H__ */
