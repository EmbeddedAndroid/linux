/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared between the arm-smmu pKVM hypervisor driver and its kernel side.
 */
#ifndef _ARM_SMMU_PKVM_H
#define _ARM_SMMU_PKVM_H

#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/types.h>

/*
 * The MMU-500 the hypervisor isolates: its base, the context banks left
 * to the host (the hypervisor takes the next one), and the range it maps
 * page by page because it is lent page by page (the default CMA area,
 * where the TEE protected heaps allocate).
 */
#define ARM_SMMU_PKVM_MAX_BANKS		3

/* The video codec's secure context banks, in DT subnode order */
enum arm_smmu_pkvm_bank_id {
	ARM_SMMU_PKVM_BANK_BITSTREAM,
	ARM_SMMU_PKVM_BANK_PIXEL,
	ARM_SMMU_PKVM_BANK_NON_PIXEL,
};

/*
 * A stream whose context bank the hypervisor owns: the host only gets to
 * map memory it lent to the secure world into it, inside the IOVA window.
 */
struct arm_smmu_pkvm_bank {
	u32	smr;		/* mask << 16 | stream ID */
	u32	iova_start;
	u32	iova_end;
};

struct arm_smmu_pkvm_params {
	u64	base;
	u64	pages_base;
	u64	pages_size;
	u32	nr_cb_host;
	u32	nr_banks;
	struct arm_smmu_pkvm_bank banks[ARM_SMMU_PKVM_MAX_BANKS];
};

enum arm_smmu_pkvm_fault_field {
	ARM_SMMU_PKVM_FSR,
	ARM_SMMU_PKVM_FAR,
	ARM_SMMU_PKVM_SYNR,
	ARM_SMMU_PKVM_TABLES,
	ARM_SMMU_PKVM_BANK_FAULT,	/* + bank: FSR << 32 | FAR */
};

#ifdef __KVM_NVHE_HYPERVISOR__
extern struct arm_smmu_pkvm_params arm_smmu_pkvm_params;
extern struct pkvm_iommu_ops arm_smmu_pkvm_ops;
u64 arm_smmu_pkvm_fault(u32 field);
int arm_smmu_pkvm_bank_map(u32 bank, u32 iova, u64 pfn, u32 nr_pages);
int arm_smmu_pkvm_bank_unmap(u32 bank, u32 iova, u32 nr_pages);
#elif IS_ENABLED(CONFIG_ARM_SMMU_PKVM)
int arm_smmu_pkvm_prot_map(unsigned int bank, u32 iova, phys_addr_t phys, size_t size);
int arm_smmu_pkvm_prot_unmap(unsigned int bank, u32 iova, size_t size);
bool arm_smmu_pkvm_prot_bank(unsigned int bank, u32 *iova_start, u32 *iova_end);
#else
static inline int arm_smmu_pkvm_prot_map(unsigned int bank, u32 iova,
					 phys_addr_t phys, size_t size)
{
	return -ENODEV;
}
static inline int arm_smmu_pkvm_prot_unmap(unsigned int bank, u32 iova, size_t size)
{
	return -ENODEV;
}
static inline bool arm_smmu_pkvm_prot_bank(unsigned int bank, u32 *iova_start,
					   u32 *iova_end)
{
	return false;
}
#endif

#endif /* _ARM_SMMU_PKVM_H */
