// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/string.h>
#include "qda_drv.h"
#include "qda_soc.h"

const struct qda_soc_data kaanapali_soc_data = {
	.dma_addr_bits_cdsp = 34,
	.dma_addr_bits_default = 32,
};

const struct qda_soc_data default_soc_data = {
	.dma_addr_bits_cdsp = 32,
	.dma_addr_bits_default = 32,
};

/**
 * qda_soc_dma_addr_bits() - DMA address width of a device's context banks
 * @qdev: QDA device
 *
 * All CDSP instances (cdsp, cdsp1, ...) share the CDSP width.
 *
 * Return: Number of DMA address bits the DSP can address on @qdev
 */
u32 qda_soc_dma_addr_bits(const struct qda_dev *qdev)
{
	if (!strncmp(qdev->dsp_name, "cdsp", 4))
		return qdev->soc_data->dma_addr_bits_cdsp;

	return qdev->soc_data->dma_addr_bits_default;
}
