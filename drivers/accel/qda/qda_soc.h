/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QDA_SOC_H__
#define __QDA_SOC_H__

#include <linux/types.h>

struct qda_dev;

/**
 * struct qda_soc_data - Per-SoC DSP integration parameters
 *
 * Selected by OF match on the fastrpc node; see qda_rpmsg.c.
 */
struct qda_soc_data {
	/** @dma_addr_bits_cdsp: DMA address width of CDSP context banks */
	u32 dma_addr_bits_cdsp;
	/** @dma_addr_bits_default: DMA address width of all other context banks */
	u32 dma_addr_bits_default;
};

extern const struct qda_soc_data kaanapali_soc_data;
extern const struct qda_soc_data default_soc_data;

u32 qda_soc_dma_addr_bits(const struct qda_dev *qdev);

#endif /* __QDA_SOC_H__ */
