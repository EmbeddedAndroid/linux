/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_DRV_H__
#define __QDA_DRV_H__

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <linux/types.h>
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

/**
 * struct qda_session - One user of a DSP
 *
 * Created either by the accel UAPI, one per open DRM file, or by the legacy
 * FastRPC compatibility shim, one per open /dev/fastrpc-* file with no DRM
 * file behind it at all. The core is keyed on this rather than on struct
 * drm_file so that it can serve both.
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
	/** @soc_data: Per-SoC DSP integration parameters */
	const struct qda_soc_data *soc_data;
	/** @dsp_name: Name of the DSP domain (e.g. "cdsp", "adsp") */
	const char *dsp_name;
	/**
	 * @legacy_auxdev: Auxiliary device the legacy FastRPC module binds
	 * to, or NULL if it could not be created
	 */
	struct auxiliary_device *legacy_auxdev;
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
void qda_session_free(struct qda_session *session);

#endif /* __QDA_DRV_H__ */
