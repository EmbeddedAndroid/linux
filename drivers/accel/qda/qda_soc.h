/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_SOC_H__
#define __QDA_SOC_H__

#include <linux/bits.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

struct qda_dev;

/**
 * struct qda_soc_data - Per-SoC DSP integration parameters
 *
 * Selected by OF match on the fastrpc node; see qda_rpmsg.c.
 */
struct qda_soc_data {
	/** @sid_pos: Bit position of the stream ID in DSP-visible addresses */
	u32 sid_pos;
	/** @dma_addr_bits_cdsp: DMA address width of CDSP context banks */
	u32 dma_addr_bits_cdsp;
	/** @dma_addr_bits_default: DMA address width of all other context banks */
	u32 dma_addr_bits_default;
};

extern const struct qda_soc_data kaanapali_soc_data;
extern const struct qda_soc_data default_soc_data;

u32 qda_soc_dma_addr_bits(const struct qda_dev *qdev);

/**
 * qda_soc_sid_offset() - Stream ID bits to fold into a DSP-visible address
 * @soc_data: SoC parameters of the device
 * @sid: Stream ID of the context bank
 *
 * Return: @sid shifted to its SoC-specific position
 */
static inline u64 qda_soc_sid_offset(const struct qda_soc_data *soc_data, u32 sid)
{
	return (u64)sid << soc_data->sid_pos;
}

/**
 * qda_soc_dma_addr() - Recover the DMA API address from a DSP-visible address
 * @soc_data: SoC parameters of the device
 * @iova: Address with the stream ID folded in
 *
 * Return: @iova with the stream ID bits cleared
 */
static inline dma_addr_t qda_soc_dma_addr(const struct qda_soc_data *soc_data, u64 iova)
{
	return iova & GENMASK_ULL(soc_data->sid_pos - 1, 0);
}

#endif /* __QDA_SOC_H__ */
