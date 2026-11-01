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

#define DRM_QDA_QUERY			0x00
#define DRM_QDA_GEM_CREATE		0x01
#define DRM_QDA_GEM_MMAP_OFFSET		0x02
#define DRM_QDA_REMOTE_INVOKE		0x03
#define DRM_QDA_REMOTE_SESSION_CREATE	0x04

#define DRM_IOCTL_QDA_QUERY		DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_QUERY, \
					 struct drm_qda_query)
#define DRM_IOCTL_QDA_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_CREATE, \
					 struct drm_qda_gem_create)
#define DRM_IOCTL_QDA_GEM_MMAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_GEM_MMAP_OFFSET, \
					 struct drm_qda_gem_mmap_offset)
#define DRM_IOCTL_QDA_REMOTE_INVOKE	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_INVOKE, \
					 struct drm_qda_invoke_args)
#define DRM_IOCTL_QDA_REMOTE_SESSION_CREATE					\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_SESSION_CREATE,		\
		 struct drm_qda_init_create)

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

/**
 * struct drm_qda_gem_create - Buffer allocation parameters
 * @size: Size of the buffer to allocate in bytes (input)
 * @handle: Handle of the new buffer (output)
 * @pad: Must be zero
 *
 * Allocates a buffer the DSP can access and returns a handle to it. The
 * handle identifies the buffer in later ioctls, and the buffer is released
 * when the last handle to it is closed.
 */
struct drm_qda_gem_create {
	__u64 size;
	__u32 handle;
	__u32 pad;
};

/**
 * struct drm_qda_gem_mmap_offset - Buffer mmap offset query
 * @offset: Offset to pass to mmap() (output)
 * @handle: Handle of the buffer (input)
 * @pad: Must be zero
 *
 * Returns the offset at which a buffer can be mmap()ed on the accel node.
 */
struct drm_qda_gem_mmap_offset {
	__u64 offset;
	__u32 handle;
	__u32 pad;
};

/**
 * struct drm_qda_fastrpc_invoke_args - FastRPC invocation argument descriptor
 * @offset: Byte offset within the buffer at which the argument starts
 * @length: Length of the argument data in bytes
 * @handle: GEM handle of the buffer holding the argument
 * @attr: Argument attributes and flags
 *
 * Describes a single argument of a FastRPC invocation. Every argument lives
 * in a buffer, named by the handle it was created or imported under, and
 * @offset and @length must describe a region within that buffer. Userspace
 * imports any DMA-BUF fd to a GEM handle before passing it here; the driver
 * never accepts DMA-BUF fds directly.
 */
struct drm_qda_fastrpc_invoke_args {
	__u64 offset;
	__u64 length;
	__u32 handle;
	__u32 attr;
};

/**
 * struct drm_qda_invoke_args - Dynamic FastRPC invocation parameters
 * @handle: Remote handle to invoke on the DSP
 * @sc: Scalars value encoding the remote method and the argument counts
 * @args: User-space pointer to an array of struct
 *        drm_qda_fastrpc_invoke_args entries, one per argument @sc describes
 *
 * Runs a method on the DSP. Results are written directly into the argument
 * buffers, so there is nothing to read back from this structure.
 */
struct drm_qda_invoke_args {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

/* Process attribute flags for drm_qda_init_create.attrs */
#define QDA_INIT_ATTR_UNSIGNED_MODULE	(1U << 3)

/**
 * struct drm_qda_init_create - DSP process creation parameters
 * @filelen: Length of the ELF image in bytes
 * @filehandle: Handle of the buffer holding the ELF image
 * @attrs: Process attribute flags
 * @siglen: Length of the signature data in bytes
 * @file: Reserved, must be zero
 *
 * Creates a process on the DSP for this file to run in. Without
 * QDA_INIT_ATTR_UNSIGNED_MODULE in @attrs the process is created signed, which
 * requires CAP_SYS_ADMIN. Creating a process again on the same file replaces
 * the first.
 */
struct drm_qda_init_create {
	__u32 filelen;
	__u32 filehandle;
	__u32 attrs;
	__u32 siglen;
	__u64 file;
};

#if defined(__cplusplus)
}
#endif

#endif /* __QDA_ACCEL_H__ */
