/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_MEMORY_MANAGER_H__
#define __QDA_MEMORY_MANAGER_H__

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct qda_dev;
struct qda_gem_obj;
struct qda_session;

/**
 * struct qda_iommu_device - A context bank available for memory allocation
 *
 * Wraps one compute context bank device. Each one is exclusively owned by at
 * most one session at a time, which is what keeps the buffers of one session
 * out of the address space of another.
 *
 * The object outlives its registration: a session holding the bank and every
 * buffer mapped into it keep a reference through @ref, so the bank can be
 * unregistered while either is still around. The device itself is destroyed
 * only once the last reference is gone, since freeing a buffer needs it.
 */
struct qda_iommu_device {
	/** @dev: The context bank device to allocate from */
	struct device *dev;
	/** @qdev: Back-pointer to the parent QDA device */
	struct qda_dev *qdev;
	/** @sid: Stream ID the DSP identifies this context bank by */
	u32 sid;
	/** @assigned_session: Session owning this device, or NULL if free */
	struct qda_session *assigned_session;
	/** @ref: Reference count controlling when the object is freed */
	struct kref ref;
	/** @lock: Protects @assigned_session */
	spinlock_t lock;
	/** @np: Context bank node @dev was created for */
	struct device_node *np;
	/** @release_work: Destroys @dev once the last reference is gone */
	struct work_struct release_work;
};

/**
 * struct qda_memory_manager - Registry of the device's context banks
 *
 * Holds every context bank of one QDA device and hands them out to sessions,
 * one each.
 */
struct qda_memory_manager {
	/** @devices: Array of registered context banks, sized to @max_devices */
	struct qda_iommu_device **devices;
	/** @max_devices: Capacity of the @devices array */
	int max_devices;
	/** @assignment_lock: Protects registration and assignment */
	struct mutex assignment_lock;
};

int qda_memory_manager_init(struct qda_memory_manager *mem_mgr, int max_devices);
void qda_memory_manager_exit(struct qda_memory_manager *mem_mgr);

int qda_memory_manager_register_device(struct qda_memory_manager *mem_mgr,
				       struct qda_iommu_device *iommu_dev);
void qda_memory_manager_unregister_device(struct qda_memory_manager *mem_mgr,
					  struct qda_iommu_device *iommu_dev);

int qda_memory_manager_assign_device(struct qda_memory_manager *mem_mgr,
				     struct qda_session *session);
void qda_memory_manager_release_device(struct qda_iommu_device *iommu_dev);
void qda_memory_manager_get_device(struct qda_iommu_device *iommu_dev);
void qda_memory_manager_put_device(struct qda_iommu_device *iommu_dev);

int qda_memory_manager_alloc(struct qda_memory_manager *mem_mgr,
			     struct qda_gem_obj *gem_obj,
			     struct qda_session *session);
void qda_memory_manager_free(struct qda_gem_obj *gem_obj);

#endif /* __QDA_MEMORY_MANAGER_H__ */
