/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __IRIS_SECURE_H__
#define __IRIS_SECURE_H__

#include <linux/types.h>

struct iris_buffer;
struct iris_core;
struct iris_inst;
struct vb2_buffer;

int iris_secure_init(struct iris_core *core);
void iris_secure_deinit(struct iris_core *core);
int iris_secure_set(struct iris_inst *inst, bool secure);
int iris_secure_buf_init(struct iris_inst *inst, struct vb2_buffer *vb2,
			 struct iris_buffer *buf);
void iris_secure_buf_cleanup(struct iris_inst *inst, struct iris_buffer *buf);
bool iris_secure_buf_refused(struct iris_inst *inst, struct vb2_buffer *vb2);
int iris_secure_alloc_internal(struct iris_inst *inst, struct iris_buffer *buf);
void iris_secure_free_internal(struct iris_inst *inst, struct iris_buffer *buf);

#endif
