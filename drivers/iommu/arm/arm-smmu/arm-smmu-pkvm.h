/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared between the arm-smmu pKVM hypervisor driver and its kernel side.
 */
#ifndef _ARM_SMMU_PKVM_H
#define _ARM_SMMU_PKVM_H

#include <linux/types.h>

/*
 * The MMU-500 the hypervisor isolates: its base, the context banks left
 * to the host (the hypervisor takes the next one), and the range it maps
 * page by page because it is lent page by page (the default CMA area,
 * where the TEE protected heaps allocate).
 */
struct arm_smmu_pkvm_params {
	u64	base;
	u64	pages_base;
	u64	pages_size;
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
u64 arm_smmu_pkvm_fault(u32 field);
#endif

#endif /* _ARM_SMMU_PKVM_H */
