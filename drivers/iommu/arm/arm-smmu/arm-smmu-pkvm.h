/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared between the arm-smmu pKVM hypervisor driver and its kernel side.
 */
#ifndef _ARM_SMMU_PKVM_H
#define _ARM_SMMU_PKVM_H

#include <linux/types.h>

/*
 * The MMU-500 the hypervisor isolates: its base, and the context banks left
 * to the host (the hypervisor takes the next one).
 */
struct arm_smmu_pkvm_params {
	u64	base;
	u32	nr_cb_host;
};

enum arm_smmu_pkvm_fault_field {
	ARM_SMMU_PKVM_FSR,
	ARM_SMMU_PKVM_FAR,
	ARM_SMMU_PKVM_SYNR,
	ARM_SMMU_PKVM_TABLES,
};

#ifdef __KVM_NVHE_HYPERVISOR__
extern struct arm_smmu_pkvm_params arm_smmu_pkvm_params;
extern struct pkvm_iommu_ops arm_smmu_pkvm_ops;
#endif

#endif /* _ARM_SMMU_PKVM_H */
