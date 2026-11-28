/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_DRV_H__
#define __QDA_DRV_H__

#include <linux/atomic.h>
#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include "qda_memory_manager.h"
#include "qda_soc.h"

/* Driver identification */
#define QDA_DRIVER_NAME "qda"

/*
 * Name of the auxiliary device the legacy FastRPC module binds to, one per
 * DSP domain. Combined with this module's name it forms the match name
 * "qda.fastrpc", which that module lists in its id_table.
 */
#define QDA_LEGACY_AUXDEV_NAME "fastrpc"

/* Upper bound on the "qcom,vmids" DT property */
#define QDA_MAX_VMIDS	16

/* Size of the cached DSP attribute array; index 0 is reserved */
#define QDA_MAX_DSP_ATTRIBUTES	256

/**
 * struct qda_session - One user of a DSP
 *
 * Created either by the accel UAPI, one per open DRM file, or by the legacy
 * FastRPC compatibility shim, one per open /dev/fastrpc-* file with no DRM
 * file behind it at all. The core is keyed on this rather than on struct
 * drm_file so that it can serve both.
 *
 * Reference counted because an invocation outlives the caller that issued it
 * when the DSP has not answered yet, and keeps using the session it belongs
 * to after the file it was opened on is closed.
 */
struct qda_session {
	/** @qda_dev: Back-pointer to device structure */
	struct qda_dev *qda_dev;
	/**
	 * @file: The DRM file this session belongs to, or NULL for a session
	 * the legacy shim created. Anything that resolves a buffer handle
	 * needs this, so those paths are only reachable from the accel UAPI.
	 */
	struct drm_file *file;
	/** @assigned_iommu_dev: Context bank owned by this session, or NULL */
	struct qda_iommu_device *assigned_iommu_dev;
	/** @remote_session_id: Identifies this session to the DSP */
	int remote_session_id;
	/** @pd: Protection domain this session's requests are directed at */
	int pd;
	/**
	 * @init_mem_gem_obj: Memory the DSP loaded this session's process
	 * into, held for as long as the process runs. Swapped with xchg(), as
	 * ioctls on one file can run concurrently.
	 */
	struct qda_gem_obj *init_mem_gem_obj;
	/**
	 * @poll_mode: Poll for completion of invocations on dynamically loaded
	 * modules in a user PD. Set through the set-option ioctl, which can run
	 * concurrently with invocations on the same file.
	 */
	bool poll_mode;
	/**
	 * @msg_buf_pool: Invoke header buffers this session is done with,
	 * kept for its later invocations to reuse.
	 */
	struct list_head msg_buf_pool;
	/** @msg_buf_pool_count: Number of entries in @msg_buf_pool */
	int msg_buf_pool_count;
	/** @msg_buf_pool_lock: Protects @msg_buf_pool and @msg_buf_pool_count */
	struct mutex msg_buf_pool_lock;
	/**
	 * @maps: Buffers mapped into the DSP process (struct qda_session_map),
	 * each holding a reference on its buffer until the DSP unmaps it or
	 * the session is released, after its process.
	 */
	struct list_head maps;
	/** @maps_lock: Protects @maps */
	struct mutex maps_lock;
	/** @ref: Dropped by the owner of the session and by every invocation */
	struct kref ref;
};

/**
 * struct qda_dev - Main device structure for QDA driver
 *
 * The DRM device is embedded as the first member so that container_of()
 * can recover the qda_dev from any drm_device pointer.
 */
struct qda_dev {
	/** @drm_dev: Embedded DRM device; recover via qda_dev_from_drm() */
	struct drm_device drm_dev;
	/** @rpdev: RPMsg device for communication with the remote processor */
	struct rpmsg_device *rpdev;
	/** @dev: Underlying Linux device */
	struct device *dev;
	/** @cb_devs: Compute context-bank (CB) child devices */
	struct list_head cb_devs;
	/** @iommu_mgr: Registry of this device's context banks */
	struct qda_memory_manager *iommu_mgr;
	/** @import_lock: Serialises PRIME imports, protects @importing_session */
	struct mutex import_lock;
	/**
	 * @importing_session: Session currently importing a buffer. Set only
	 * while @import_lock is held, to pass the session down to the
	 * .gem_prime_import callback, which the DRM core does not hand the
	 * calling file to.
	 */
	struct qda_session *importing_session;
	/** @ctx_xa: Invocations in flight, keyed by context ID */
	struct xarray ctx_xa;
	/** @ctx_seq: Distinguishes invocations that reuse a context ID */
	atomic64_t ctx_seq;
	/** @session_id_seq: Hands out the session IDs the DSP is told */
	atomic_t session_id_seq;
	/** @soc_data: Per-SoC DSP integration parameters */
	const struct qda_soc_data *soc_data;
	/** @dsp_name: Name of the DSP domain (e.g. "cdsp", "adsp") */
	const char *dsp_name;
	/**
	 * @legacy_auxdev: Auxiliary device the legacy FastRPC module binds
	 * to, or NULL if it could not be created
	 */
	struct auxiliary_device *legacy_auxdev;
	/** @attr_lock: Serialises the DSP capability query and protects the cache below */
	struct mutex attr_lock;
	/** @valid_attributes: True once @dsp_attributes has been queried from the DSP */
	bool valid_attributes;
	/** @dsp_attributes: Cached DSP capability attributes */
	u32 dsp_attributes[QDA_MAX_DSP_ATTRIBUTES];
	/** @vmperms: VMs the remote heap is shared with, from qcom,vmids */
	struct qcom_scm_vmperm vmperms[QDA_MAX_VMIDS];
	/** @vmcount: Number of valid entries in @vmperms */
	int vmcount;
	/** @remote_heap_addr: Physical base of the reserved region, or 0 */
	phys_addr_t remote_heap_addr;
	/** @remote_heap_size: Size of the reserved region, or 0 */
	u64 remote_heap_size;
	/**
	 * @remote_heap_lock: Held across an Audio PD attach, so the heap is
	 * handed to the DSP by exactly one of them. Protects @audio_init_mem.
	 */
	struct mutex remote_heap_lock;
	/** @audio_init_mem: True once the DSP has been given the remote heap */
	bool audio_init_mem;
	/** @poll_mode_supported: Sessions may opt in to invocation completion polling */
	bool poll_mode_supported;
};

/**
 * qda_dev_from_drm - Recover qda_dev from an embedded drm_device pointer
 * @dev: Pointer to the embedded drm_device
 *
 * Return: Pointer to the enclosing qda_dev.
 */
static inline struct qda_dev *qda_dev_from_drm(struct drm_device *dev)
{
	return container_of(dev, struct qda_dev, drm_dev);
}

/* Device allocation (uses devm_drm_dev_alloc internally) */
struct qda_dev *qda_alloc_device(struct device *dev);

/* Core device lifecycle */
int qda_init_device(struct qda_dev *qdev, int num_cbs);
void qda_deinit_device(struct qda_dev *qdev);
int qda_register_device(struct qda_dev *qdev);
void qda_unregister_device(struct qda_dev *qdev);

/* Sessions, shared with the legacy shim */
struct qda_session *qda_session_alloc(struct qda_dev *qdev);
void qda_session_get(struct qda_session *session);
void qda_session_put(struct qda_session *session);

/* DSP process management */
int qda_release_dsp_process(struct qda_dev *qdev, struct qda_session *session);

#endif /* __QDA_DRV_H__ */
