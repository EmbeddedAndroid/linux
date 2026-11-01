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
#define DRM_QDA_REMOTE_MMAP		0x05
#define DRM_QDA_REMOTE_MUNMAP		0x06
#define DRM_QDA_REMOTE_SESSION_ATTACH	0x07
#define DRM_QDA_SET_OPTION		0x08

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
#define DRM_IOCTL_QDA_REMOTE_MMAP	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_MMAP, \
					 struct drm_qda_mem_map)
#define DRM_IOCTL_QDA_REMOTE_MUNMAP	DRM_IOWR(DRM_COMMAND_BASE + DRM_QDA_REMOTE_MUNMAP, \
					 struct drm_qda_mem_unmap)
#define DRM_IOCTL_QDA_REMOTE_SESSION_ATTACH					\
	DRM_IOW(DRM_COMMAND_BASE + DRM_QDA_REMOTE_SESSION_ATTACH,		\
		struct drm_qda_remote_attach)
#define DRM_IOCTL_QDA_SET_OPTION	DRM_IOW(DRM_COMMAND_BASE + DRM_QDA_SET_OPTION, \
					 struct drm_qda_set_option)

/* Request types for drm_qda_mem_map.request */
#define QDA_REQUEST_MMAP	1	/* Map a whole buffer */
#define QDA_REQUEST_MEM_MAP	2	/* Map a region of a buffer, with attributes */

/* Request types for drm_qda_mem_unmap.request */
#define QDA_REQUEST_MUNMAP	1
#define QDA_REQUEST_MEM_UNMAP	2

/* Query type definitions for drm_qda_query */
#define QDA_QUERY_DSP_NAME		1
#define QDA_QUERY_DSP_CAPABILITIES	2

/**
 * struct drm_qda_query - Device information query structure
 * @query_type: Type of query (input)
 * @dsp_name: Null-terminated name of the DSP (returned when query_type is
 *            QDA_QUERY_DSP_NAME)
 * @cap: DSP capability query (used when query_type is
 *       QDA_QUERY_DSP_CAPABILITIES): @cap.attribute_id selects the attribute
 *       (input) and @cap.capability returns its value (output)
 *
 * This structure is used with DRM_IOCTL_QDA_QUERY to query device attributes
 * based on @query_type. @dsp_name and @cap share storage, as only one query
 * is made per call.
 */
struct drm_qda_query {
	__u32 query_type;
	union {
		__u8 dsp_name[16];
		struct {
			__u32 attribute_id;
			__u32 capability;
		} cap;
	};
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

/**
 * struct drm_qda_mem_map - Parameters for mapping a buffer into the DSP
 * @request: QDA_REQUEST_MMAP or QDA_REQUEST_MEM_MAP
 * @flags: Mapping flags for the DSP (cache attributes, permissions)
 * @handle: Handle of the buffer to map
 * @attrs: Mapping attributes (QDA_REQUEST_MEM_MAP)
 * @offset: Offset within the buffer (QDA_REQUEST_MEM_MAP), at most INT_MAX
 * @dsp_handle: Identifier the DSP tracks the buffer by (QDA_REQUEST_MEM_MAP)
 * @vaddrin: Address the DSP should map the buffer at, if it can (input)
 * @size: Length of the region to map in bytes, or 0 for the whole buffer
 * @vaddrout: Address the DSP mapped the buffer at (output)
 *
 * Makes a buffer visible to the DSP of this file ahead of the calls that use
 * it. QDA_REQUEST_MMAP maps a whole buffer and uses @handle, @flags,
 * @vaddrin, @size and @vaddrout. QDA_REQUEST_MEM_MAP maps a region of it with
 * caller-chosen attributes and uses every field.
 */
struct drm_qda_mem_map {
	__u32 request;
	__u32 flags;
	__u32 handle;
	__u32 attrs;
	__u32 offset;
	__s32 dsp_handle;
	__u64 vaddrin;
	__u64 size;
	__u64 vaddrout;
};

/**
 * struct drm_qda_mem_unmap - Parameters for removing a DSP mapping
 * @request: QDA_REQUEST_MUNMAP or QDA_REQUEST_MEM_UNMAP
 * @dsp_handle: Identifier the DSP tracks the buffer by (QDA_REQUEST_MEM_UNMAP)
 * @vaddr: Address the mapping was made at (QDA_REQUEST_MEM_UNMAP)
 * @vaddrout: Address the mapping was made at (QDA_REQUEST_MUNMAP)
 * @size: Length of the mapping in bytes
 *
 * Removes a mapping made with DRM_IOCTL_QDA_REMOTE_MMAP, named by the address
 * that call returned in drm_qda_mem_map.vaddrout. The request must match the
 * one the mapping was made with: QDA_REQUEST_MUNMAP uses @vaddrout and @size,
 * QDA_REQUEST_MEM_UNMAP uses @dsp_handle, @vaddr and @size. The buffer is not
 * affected, so its handle must stay open for as long as the DSP uses the
 * mapping.
 */
struct drm_qda_mem_unmap {
	__u32 request;
	__s32 dsp_handle;
	__u64 vaddr;
	__u64 vaddrout;
	__u64 size;
};

/* Protection domains for drm_qda_remote_attach.pd_type */
#define QDA_ATTACH_ROOT_PD	1
#define QDA_ATTACH_SENSORS_PD	2
#define QDA_ATTACH_AUDIO_PD	3

/**
 * struct drm_qda_remote_attach - Parameters for attaching to a DSP protection domain
 * @pd_type: QDA_ATTACH_ROOT_PD, QDA_ATTACH_SENSORS_PD or QDA_ATTACH_AUDIO_PD
 * @namelen: Length of the name in bytes including the NUL terminator
 *           (QDA_ATTACH_AUDIO_PD only, otherwise must be zero)
 * @name: User pointer to the NUL-terminated name of the static PD to attach
 *        to (QDA_ATTACH_AUDIO_PD only, otherwise must be zero)
 *
 * Binds this file to a protection domain that already exists on the DSP,
 * instead of creating one with DRM_IOCTL_QDA_REMOTE_SESSION_CREATE. Every
 * later request on the file is directed at it. Fails with EBUSY if a process
 * has already been created on the file.
 *
 * For QDA_ATTACH_AUDIO_PD, the first successful attach on a device also
 * hands the DSP the remote heap reserved for it in the device tree; later
 * attaches reuse that heap.
 *
 * Requires CAP_SYS_ADMIN.
 */
struct drm_qda_remote_attach {
	__u32 pd_type;
	__u32 namelen;
	__u64 name;
};

/* Request IDs for drm_qda_set_option.request_id */
#define QDA_SET_OPTION_POLL_MODE	1

/* Values for drm_qda_set_option.value with QDA_SET_OPTION_POLL_MODE */
#define QDA_POLL_MODE_DISABLE	0
#define QDA_POLL_MODE_ENABLE	1

/**
 * struct drm_qda_set_option - Set a per-session option
 * @request_id: Option to set (QDA_SET_OPTION_*)
 * @value: Request-specific value
 *
 * For QDA_SET_OPTION_POLL_MODE, @value is QDA_POLL_MODE_ENABLE or
 * QDA_POLL_MODE_DISABLE. Enabling poll mode lets the driver poll shared
 * memory for completion of a dynamically loaded module's invocations in a
 * user PD instead of waiting for an interrupt-based response, trading CPU
 * cycles for lower round-trip latency. DSPs that do not support it reject
 * the request with EOPNOTSUPP.
 */
struct drm_qda_set_option {
	__u32 request_id;
	__u32 value;
};

#if defined(__cplusplus)
}
#endif

#endif /* __QDA_ACCEL_H__ */
