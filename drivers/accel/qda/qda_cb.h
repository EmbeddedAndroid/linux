/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_CB_H__
#define __QDA_CB_H__

#include <linux/list.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "qda_drv.h"

struct qda_iommu_device;

/**
 * struct qda_cb_dev - Bookkeeping for one compute context bank device
 */
struct qda_cb_dev {
	/** @node: Entry in &qda_dev.cb_devs */
	struct list_head node;
	/** @pdev: Platform device created for the context bank node */
	struct platform_device *pdev;
};

/* Create a device for each context bank child node of @parent_node */
int qda_cb_populate(struct qda_dev *qdev, struct device_node *parent_node);
/* Unregister every context bank qda_cb_populate() added */
void qda_cb_unpopulate(struct qda_dev *qdev);
/* Destroy the device of a bank nothing references any more */
void qda_cb_release(struct qda_iommu_device *iommu_dev);
int qda_cb_init(void);
void qda_cb_exit(void);

#endif /* __QDA_CB_H__ */
