// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#include <linux/of.h>
#include <linux/string.h>
#include "qda_drv.h"
#include "qda_soc.h"

const struct qda_soc_data kaanapali_soc_data = {
	.sid_pos = 56,
	.dma_addr_bits_cdsp = 34,
	.dma_addr_bits_default = 32,
	.poll_mode_supported = true,
};

const struct qda_soc_data default_soc_data = {
	.sid_pos = 32,
	.dma_addr_bits_cdsp = 32,
	.dma_addr_bits_default = 32,
};

/*
 * Platforms that share default_soc_data but whose DSP firmware does implement
 * invocation completion polling. The list is closed: new SoCs advertise
 * polling support through their own qda_soc_data instead.
 */
static const struct of_device_id qda_poll_mode_machines[] = {
	{ .compatible = "qcom,milos" },
	{ .compatible = "qcom,qcs8300" },
	{ .compatible = "qcom,sa8775p" },
	{ .compatible = "qcom,sar2130p" },
	{ .compatible = "qcom,sm8450" },
	{ .compatible = "qcom,sm8550" },
	{ .compatible = "qcom,sm8650" },
	{ .compatible = "qcom,sm8750" },
	{ .compatible = "qcom,x1e80100" },
	{ .compatible = "qcom,x1p42100" },
	{ }
};

/**
 * qda_soc_poll_mode_supported() - Whether the DSP supports completion polling
 * @soc_data: SoC parameters of the device
 *
 * Return: true if invocations on this SoC may use poll mode
 */
bool qda_soc_poll_mode_supported(const struct qda_soc_data *soc_data)
{
	return soc_data->poll_mode_supported ||
	       of_machine_get_match(qda_poll_mode_machines);
}

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
