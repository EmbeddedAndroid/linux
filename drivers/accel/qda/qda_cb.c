// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include "qda_cb.h"
#include "qda_drv.h"
#include "qda_memory_manager.h"
#include "qda_soc.h"

/*
 * A bank's device lives until the last buffer mapped into it is freed, which
 * can be long after the DSP restarted and its QDA device was replaced. The
 * lock orders creating and destroying bank devices against the list of QDA
 * devices a released bank is added back to.
 */
static DEFINE_MUTEX(qda_cb_lock);
static LIST_HEAD(qda_cb_owners);
static struct workqueue_struct *qda_cb_wq;

static void qda_cb_release_work(struct work_struct *work);

static int qda_cb_setup_device(struct qda_dev *qdev, struct device *cb_dev, u32 sid)
{
	struct qda_iommu_device *iommu_dev;
	int ret;

	iommu_dev = kzalloc_obj(*iommu_dev);
	if (!iommu_dev)
		return -ENOMEM;

	iommu_dev->dev = cb_dev;
	iommu_dev->qdev = qdev;
	iommu_dev->sid = sid;
	INIT_WORK(&iommu_dev->release_work, qda_cb_release_work);
	iommu_dev->np = of_node_get(cb_dev->of_node);
	dev_set_drvdata(cb_dev, iommu_dev);

	/* A bank added back to a live device can be assigned once registered */
	ret = qda_memory_manager_register_device(qdev->iommu_mgr, iommu_dev);
	if (ret) {
		drm_err(&qdev->drm_dev, "Failed to register context bank: %d\n", ret);
		dev_set_drvdata(cb_dev, NULL);
		of_node_put(iommu_dev->np);
		kfree(iommu_dev);
		return ret;
	}

	/* Buffers still look up the SoC data through @qdev when freed */
	drm_dev_get(&qdev->drm_dev);

	return 0;
}

static int qda_create_cb_device(struct qda_dev *qdev, struct device_node *cb_node)
{
	struct qda_cb_dev *entry;
	struct platform_device *pdev;
	u32 dma_bits;
	u32 sid = 0;
	int ret;

	lockdep_assert_held(&qda_cb_lock);

	/*
	 * The stream ID identifies the context bank to the DSP. A node without
	 * one is accepted so that buffers can still be allocated from it, but
	 * the DSP will then see every such bank as stream ID 0.
	 */
	if (of_property_read_u32(cb_node, "reg", &sid))
		drm_info(&qdev->drm_dev, "No stream ID in %pOFn, using 0\n", cb_node);

	entry = kzalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;

	pdev = of_platform_device_create(cb_node, NULL, qdev->dev);
	if (!pdev) {
		drm_err(&qdev->drm_dev, "Failed to create CB device for %pOFn\n", cb_node);
		ret = -ENODEV;
		goto err_free;
	}

	/*
	 * No driver binds to the bank, so the only DMA configuration it got
	 * ran while the IOMMU core probed it, before it had a domain.
	 */
	ret = of_dma_configure(&pdev->dev, cb_node, true);
	if (ret) {
		drm_err(&qdev->drm_dev, "DMA configuration failed for %pOFn: %d\n",
			cb_node, ret);
		goto err_destroy;
	}

	dma_bits = qda_soc_dma_addr_bits(qdev);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		drm_err(&qdev->drm_dev, "%u-bit DMA enable failed for %pOFn: %d\n",
			dma_bits, cb_node, ret);
		goto err_destroy;
	}

	ret = qda_cb_setup_device(qdev, &pdev->dev, sid);
	if (ret)
		goto err_destroy;

	entry->pdev = pdev;
	list_add_tail(&entry->node, &qdev->cb_devs);

	return 0;

err_destroy:
	of_platform_device_destroy(&pdev->dev, NULL);
err_free:
	kfree(entry);
	return ret;
}

static void qda_cb_release_work(struct work_struct *work)
{
	struct qda_iommu_device *iommu_dev =
		container_of(work, struct qda_iommu_device, release_work);
	struct drm_device *drm_dev = &iommu_dev->qdev->drm_dev;
	struct device_node *np = iommu_dev->np;
	struct qda_dev *qdev;
	int ret;

	scoped_guard(mutex, &qda_cb_lock) {
		dev_set_drvdata(iommu_dev->dev, NULL);
		of_platform_device_destroy(iommu_dev->dev, NULL);
		kfree(iommu_dev);

		/* The DSP came back while the bank was busy: hand it over now */
		list_for_each_entry(qdev, &qda_cb_owners, cb_owner) {
			if (qdev->dev->of_node == np->parent) {
				ret = qda_create_cb_device(qdev, np);
				if (ret)
					drm_warn(&qdev->drm_dev, "%pOFn not added back: %d\n",
						 np, ret);
				break;
			}
		}
	}

	of_node_put(np);
	drm_dev_put(drm_dev);
}

/**
 * qda_cb_release() - Destroy a context bank nothing references any more
 * @iommu_dev: Context bank whose last reference was just dropped
 *
 * The last reference can be dropped by qda_cb_unpopulate() with qda_cb_lock
 * held, so destroying the device is deferred to a work item.
 */
void qda_cb_release(struct qda_iommu_device *iommu_dev)
{
	queue_work(qda_cb_wq, &iommu_dev->release_work);
}

static void __qda_cb_unpopulate(struct qda_dev *qdev)
{
	struct qda_cb_dev *entry, *tmp;

	list_del_init(&qdev->cb_owner);

	list_for_each_entry_safe(entry, tmp, &qdev->cb_devs, node) {
		struct qda_iommu_device *iommu_dev = dev_get_drvdata(&entry->pdev->dev);

		list_del(&entry->node);
		qda_memory_manager_unregister_device(qdev->iommu_mgr, iommu_dev);
		kfree(entry);
	}
}

int qda_cb_populate(struct qda_dev *qdev, struct device_node *parent_node)
{
	int ret;

	guard(mutex)(&qda_cb_lock);

	list_add_tail(&qdev->cb_owner, &qda_cb_owners);

	for_each_available_child_of_node_scoped(parent_node, child) {
		if (!of_device_is_compatible(child, "qcom,fastrpc-compute-cb"))
			continue;

		/*
		 * Still holding buffers from before the DSP restarted. It is
		 * added once they are freed, so that no new session can reach
		 * them through the bank.
		 */
		if (of_node_check_flag(child, OF_POPULATED))
			continue;

		ret = qda_create_cb_device(qdev, child);
		if (ret) {
			__qda_cb_unpopulate(qdev);
			return ret;
		}
	}

	return 0;
}

void qda_cb_unpopulate(struct qda_dev *qdev)
{
	guard(mutex)(&qda_cb_lock);

	__qda_cb_unpopulate(qdev);
}

int qda_cb_init(void)
{
	qda_cb_wq = alloc_workqueue("qda_cb", WQ_UNBOUND | WQ_FREEZABLE, 0);

	return qda_cb_wq ? 0 : -ENOMEM;
}

void qda_cb_exit(void)
{
	destroy_workqueue(qda_cb_wq);
}
