/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NVHE_IOMMU_H__
#define __ARM64_KVM_NVHE_IOMMU_H__

#include <asm/kvm_host.h>
#include <asm/kvm_pgtable.h>

struct pkvm_iommu_ops {
	int (*init)(void);
	/*
	 * Mirror [start, end) of the host stage-2 with IOMMU_* @prot, where 0
	 * unmaps. Called with host_mmu.lock held. A range may already be mapped
	 * with the same @prot, which must succeed.
	 */
	int (*host_stage2_idmap)(phys_addr_t start, phys_addr_t end, int prot);
	/*
	 * Emulate a host data abort on MMIO @addr
	 * @esr has a valid instruction syndrome and @regs holds the host x0-x30,
	 * SRT 31 is the zero register. Return true if the access was emulated,
	 * which would skip the instruction.
	 */
	bool (*dabt_handler)(struct user_pt_regs *regs, u64 esr, u64 addr);
};

int pkvm_iommu_init(void *pool_base, unsigned int nr_pages);

int pkvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				 enum kvm_pgtable_prot prot);

/* Allocate pages from the IOMMU carveout, returns zeroed memory. */
void *pkvm_iommu_alloc_pages(u8 order);
/* Free pages from pkvm_iommu_alloc_pages(). */
void pkvm_iommu_free_pages(void *ptr);

bool pkvm_iommu_host_dabt_handler(struct user_pt_regs *regs, u64 esr, u64 addr);

#endif /* __ARM64_KVM_NVHE_IOMMU_H__ */
