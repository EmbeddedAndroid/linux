/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared between the arm-smmu pKVM hypervisor driver and its kernel side.
 */
#ifndef _ARM_SMMU_PKVM_H
#define _ARM_SMMU_PKVM_H

#include <linux/types.h>

/*
 * Context banks the hypervisor keeps, below its shadow bank, for streams
 * it owns on the host's behalf (include/linux/iommu-prot.h), one per
 * stream declared before the hypervisor initialises.
 */
#define ARM_SMMU_PKVM_PROT_BANKS	3

/*
 * The MMU-500 the hypervisor isolates: its base, the context banks left
 * to the host (the hypervisor takes the next ones), how many table pages
 * each protected bank may hold (the host picks the IOVA: one bank must not
 * starve the others, and a map over the cap fails with -ENOMEM), and the
 * streams declared for the protected banks.
 */
struct arm_smmu_pkvm_params {
	u64	base;
	u32	nr_cb_host;
	u32	bank_tables;
	u32	nr_prot;
	u32	prot_smr[ARM_SMMU_PKVM_PROT_BANKS];	/* mask << 16 | ID */
};

enum arm_smmu_pkvm_fault_field {
	ARM_SMMU_PKVM_FSR,
	ARM_SMMU_PKVM_FAR,
	ARM_SMMU_PKVM_SYNR,
	ARM_SMMU_PKVM_TABLES,
	ARM_SMMU_PKVM_BANK_FAULT,	/* + bank: FSR << 32 | FAR */
	ARM_SMMU_PKVM_BANK_SMR = ARM_SMMU_PKVM_BANK_FAULT + ARM_SMMU_PKVM_PROT_BANKS,
					/* + bank: its stream, 0 if none */
};

#ifdef __KVM_NVHE_HYPERVISOR__
extern struct arm_smmu_pkvm_params arm_smmu_pkvm_params;
extern struct pkvm_iommu_ops arm_smmu_pkvm_ops;
#endif

#endif /* _ARM_SMMU_PKVM_H */
