/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Qualcomm iris video codec driver: userspace API
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __UAPI_MEDIA_QCOM_IRIS_H
#define __UAPI_MEDIA_QCOM_IRIS_H

#include <linux/v4l2-controls.h>

/*
 * Secure session (boolean): the session decodes or encodes only in
 * protected memory. Set it before the first VIDIOC_REQBUFS of the session.
 */
#define V4L2_CID_IRIS_SECURE_SESSION	(V4L2_CID_USER_QCOM_IRIS_BASE + 0x1)

#endif /* __UAPI_MEDIA_QCOM_IRIS_H */
