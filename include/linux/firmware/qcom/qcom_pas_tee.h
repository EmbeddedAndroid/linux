/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm Peripheral Authentication Service (PAS) OP-TEE backend.
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2026 Linaro Ltd.
 */
#ifndef __QCOM_PAS_TEE_H
#define __QCOM_PAS_TEE_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_QCOM_PAS_TEE)

bool qcom_pas_tee_available(void);
int qcom_pas_tee_init_image(u32 pas_id, const void *metadata, size_t size);
int qcom_pas_tee_mem_setup(u32 pas_id, phys_addr_t addr, phys_addr_t size);
int qcom_pas_tee_auth_and_reset(u32 pas_id, phys_addr_t addr, phys_addr_t size);
int qcom_pas_tee_shutdown(u32 pas_id);

#else /* CONFIG_QCOM_PAS_TEE */

static inline bool qcom_pas_tee_available(void)
{
	return false;
}

static inline int qcom_pas_tee_init_image(u32 pas_id, const void *metadata,
					  size_t size)
{
	return -EOPNOTSUPP;
}

static inline int qcom_pas_tee_mem_setup(u32 pas_id, phys_addr_t addr,
					 phys_addr_t size)
{
	return -EOPNOTSUPP;
}

static inline int qcom_pas_tee_auth_and_reset(u32 pas_id, phys_addr_t addr,
					      phys_addr_t size)
{
	return -EOPNOTSUPP;
}

static inline int qcom_pas_tee_shutdown(u32 pas_id)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_QCOM_PAS_TEE */

#endif /* __QCOM_PAS_TEE_H */
