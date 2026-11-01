// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <drm/drm_print.h>
#include "qda_drv.h"
#include "qda_gem.h"
#include "qda_memory_dma.h"
#include "qda_memory_manager.h"

static void iommu_device_free(struct kref *ref)
{
	kfree(container_of(ref, struct qda_iommu_device, ref));
}

static struct qda_iommu_device *assign_available_device(struct qda_memory_manager *mem_mgr,
							struct qda_session *session)
{
	struct qda_iommu_device *selected_dev = NULL;
	int i;

	for (i = 0; i < mem_mgr->max_devices; i++) {
		struct qda_iommu_device *iommu_dev = mem_mgr->devices[i];

		if (!iommu_dev)
			continue;

		guard(spinlock_irqsave)(&iommu_dev->lock);
		if (!iommu_dev->assigned_session) {
			iommu_dev->assigned_session = session;
			selected_dev = iommu_dev;
			kref_get(&selected_dev->ref);
			break;
		}
	}

	return selected_dev;
}

/**
 * qda_memory_manager_assign_device() - Give a session a context bank
 * @mem_mgr: Memory manager of the device
 * @session: Session to assign a context bank to
 *
 * Assigns a free context bank to @session, which then owns it exclusively
 * until the session is released. Does nothing if the session already has
 * one.
 *
 * On success the session owns a reference on the context bank and must
 * release it with qda_memory_manager_release_device().
 *
 * Return: 0 on success, -EBUSY if every context bank is already taken
 */
int qda_memory_manager_assign_device(struct qda_memory_manager *mem_mgr,
				     struct qda_session *session)
{
	struct qda_iommu_device *selected_dev;

	guard(mutex)(&mem_mgr->assignment_lock);

	if (session->assigned_iommu_dev)
		return 0;

	selected_dev = assign_available_device(mem_mgr, session);
	if (!selected_dev) {
		drm_dbg_driver(&session->qda_dev->drm_dev, "No context bank available\n");
		return -EBUSY;
	}

	session->assigned_iommu_dev = selected_dev;

	return 0;
}

/**
 * qda_memory_manager_release_device() - Release a session's context bank
 * @iommu_dev: Device previously obtained from qda_memory_manager_assign_device()
 *
 * Drops the session's reference and frees the context bank up for the next
 * session.
 */
void qda_memory_manager_release_device(struct qda_iommu_device *iommu_dev)
{
	scoped_guard(spinlock_irqsave, &iommu_dev->lock)
		iommu_dev->assigned_session = NULL;

	kref_put(&iommu_dev->ref, iommu_device_free);
}

/**
 * qda_memory_manager_alloc() - Allocate a buffer for a session
 * @mem_mgr: Memory manager of the device
 * @gem_obj: GEM object to back with the allocation
 * @session: Session the buffer belongs to
 *
 * Allocates from the context bank the session owns, assigning it one on its
 * first allocation.
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_memory_manager_alloc(struct qda_memory_manager *mem_mgr, struct qda_gem_obj *gem_obj,
			     struct qda_session *session)
{
	int ret;

	ret = qda_memory_manager_assign_device(mem_mgr, session);
	if (ret)
		return ret;

	return qda_dma_alloc(session->assigned_iommu_dev, gem_obj, gem_obj->size);
}

/**
 * qda_memory_manager_free() - Release a buffer
 * @gem_obj: GEM object to free
 *
 * The buffer remembers the context bank it was allocated from, so no
 * memory manager is needed here.
 */
void qda_memory_manager_free(struct qda_gem_obj *gem_obj)
{
	qda_dma_free(gem_obj);
}

/**
 * qda_memory_manager_register_device() - Add a context bank to the registry
 * @mem_mgr: Memory manager of the device
 * @iommu_dev: Context bank to register
 *
 * Return: 0 on success, -ENOSPC if the registry is full
 */
int qda_memory_manager_register_device(struct qda_memory_manager *mem_mgr,
				       struct qda_iommu_device *iommu_dev)
{
	int i;

	spin_lock_init(&iommu_dev->lock);
	kref_init(&iommu_dev->ref);
	iommu_dev->assigned_session = NULL;

	guard(mutex)(&mem_mgr->assignment_lock);
	for (i = 0; i < mem_mgr->max_devices; i++) {
		if (!mem_mgr->devices[i]) {
			mem_mgr->devices[i] = iommu_dev;
			return 0;
		}
	}

	return -ENOSPC;
}

/**
 * qda_memory_manager_unregister_device() - Remove a context bank
 * @mem_mgr: Memory manager of the device
 * @iommu_dev: Context bank to unregister
 *
 * Takes the registry's reference away. The object itself stays alive until
 * any session still holding it is released.
 */
void qda_memory_manager_unregister_device(struct qda_memory_manager *mem_mgr,
					  struct qda_iommu_device *iommu_dev)
{
	int i;

	scoped_guard(mutex, &mem_mgr->assignment_lock) {
		for (i = 0; i < mem_mgr->max_devices; i++) {
			if (mem_mgr->devices[i] == iommu_dev) {
				mem_mgr->devices[i] = NULL;
				break;
			}
		}
	}

	kref_put(&iommu_dev->ref, iommu_device_free);
}

/**
 * qda_memory_manager_init() - Initialise the context bank registry
 * @mem_mgr: Memory manager to initialise
 * @max_devices: Number of context banks to make room for
 *
 * Return: 0 on success, negative error code on failure
 */
int qda_memory_manager_init(struct qda_memory_manager *mem_mgr, int max_devices)
{
	if (max_devices <= 0)
		return -EINVAL;

	mem_mgr->devices = kcalloc(max_devices, sizeof(*mem_mgr->devices), GFP_KERNEL);
	if (!mem_mgr->devices)
		return -ENOMEM;

	mem_mgr->max_devices = max_devices;
	mutex_init(&mem_mgr->assignment_lock);

	return 0;
}

/**
 * qda_memory_manager_exit() - Tear down the context bank registry
 * @mem_mgr: Memory manager to tear down
 *
 * Context banks are unregistered as their devices are destroyed, so the
 * registry is expected to be empty by the time this runs.
 */
void qda_memory_manager_exit(struct qda_memory_manager *mem_mgr)
{
	kfree(mem_mgr->devices);
	mem_mgr->devices = NULL;
	mem_mgr->max_devices = 0;
	mutex_destroy(&mem_mgr->assignment_lock);
}
