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
/* Destroy every context bank device created by qda_cb_populate() */
void qda_cb_unpopulate(struct qda_dev *qdev);

#endif /* __QDA_CB_H__ */
