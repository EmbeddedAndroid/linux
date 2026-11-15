// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <drm/drm_print.h>
#include "qda_cb.h"
#include "qda_drv.h"
#include "qda_memory_manager.h"
#include "qda_soc.h"

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

	ret = qda_memory_manager_register_device(qdev->iommu_mgr, iommu_dev);
	if (ret) {
		drm_err(&qdev->drm_dev, "Failed to register context bank: %d\n", ret);
		kfree(iommu_dev);
		return ret;
	}

	dev_set_drvdata(cb_dev, iommu_dev);

	return 0;
}

static void qda_cb_teardown_device(struct device *cb_dev)
{
	struct qda_iommu_device *iommu_dev = dev_get_drvdata(cb_dev);

	if (!iommu_dev)
		return;

	qda_memory_manager_unregister_device(iommu_dev->qdev->iommu_mgr, iommu_dev);
	dev_set_drvdata(cb_dev, NULL);
}

static int qda_create_cb_device(struct qda_dev *qdev, struct device_node *cb_node)
{
	struct qda_cb_dev *entry;
	struct platform_device *pdev;
	u32 dma_bits;
	u32 sid = 0;
	int ret;

	/*
	 * The stream ID identifies the context bank to the DSP. A node without
	 * one is accepted so that buffers can still be allocated from it, but
	 * the DSP will then see every such bank as stream ID 0.
	 */
	if (of_property_read_u32(cb_node, "reg", &sid))
		drm_info(&qdev->drm_dev, "No stream ID in %pOFn, using 0\n", cb_node);

	pdev = of_platform_device_create(cb_node, NULL, qdev->dev);
	if (!pdev) {
		drm_err(&qdev->drm_dev, "Failed to create CB device for %pOFn\n", cb_node);
		return -ENODEV;
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

	entry = kzalloc_obj(*entry);
	if (!entry) {
		ret = -ENOMEM;
		goto err_teardown;
	}

	entry->pdev = pdev;
	list_add_tail(&entry->node, &qdev->cb_devs);

	return 0;

err_teardown:
	qda_cb_teardown_device(&pdev->dev);
err_destroy:
	of_platform_device_destroy(&pdev->dev, NULL);
	return ret;
}

int qda_cb_populate(struct qda_dev *qdev, struct device_node *parent_node)
{
	int ret;

	for_each_available_child_of_node_scoped(parent_node, child) {
		if (!of_device_is_compatible(child, "qcom,fastrpc-compute-cb"))
			continue;

		ret = qda_create_cb_device(qdev, child);
		if (ret) {
			qda_cb_unpopulate(qdev);
			return ret;
		}
	}

	return 0;
}

void qda_cb_unpopulate(struct qda_dev *qdev)
{
	struct qda_cb_dev *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &qdev->cb_devs, node) {
		list_del(&entry->node);
		qda_cb_teardown_device(&entry->pdev->dev);
		of_platform_device_destroy(&entry->pdev->dev, NULL);
		kfree(entry);
	}
}
