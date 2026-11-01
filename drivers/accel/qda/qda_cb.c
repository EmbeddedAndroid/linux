// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <drm/drm_print.h>
#include "qda_cb.h"
#include "qda_drv.h"
#include "qda_soc.h"

static int qda_create_cb_device(struct qda_dev *qdev, struct device_node *cb_node)
{
	struct qda_cb_dev *entry;
	struct platform_device *pdev;
	u32 dma_bits;
	int ret;

	pdev = of_platform_device_create(cb_node, NULL, qdev->dev);
	if (!pdev) {
		drm_err(&qdev->drm_dev, "Failed to create CB device for %pOFn\n", cb_node);
		return -ENODEV;
	}

	dma_bits = qda_soc_dma_addr_bits(qdev);
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		drm_err(&qdev->drm_dev, "%u-bit DMA enable failed for %pOFn: %d\n",
			dma_bits, cb_node, ret);
		goto err_destroy;
	}

	entry = kzalloc_obj(*entry);
	if (!entry) {
		ret = -ENOMEM;
		goto err_destroy;
	}

	entry->pdev = pdev;
	list_add_tail(&entry->node, &qdev->cb_devs);

	return 0;

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
		of_platform_device_destroy(&entry->pdev->dev, NULL);
		kfree(entry);
	}
}
