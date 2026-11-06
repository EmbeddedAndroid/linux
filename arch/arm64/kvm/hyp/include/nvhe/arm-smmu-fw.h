/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_NVHE_ARM_SMMU_FW_H__
#define __KVM_NVHE_ARM_SMMU_FW_H__

#include <linux/types.h>
#include <asm/ptrace.h>

#ifdef CONFIG_PKVM_ARM_SMMU_FW
int pkvm_arm_smmu_fw_init(void);
/*
 * Emulate a host data abort on @addr, which carries a valid instruction
 * syndrome in @esr. Return true if the access was emulated and the host
 * instruction is to be skipped.
 */
bool pkvm_arm_smmu_fw_dabt_handler(struct user_pt_regs *regs, u64 esr,
				   u64 addr);
#else
static inline int pkvm_arm_smmu_fw_init(void)
{
	return 0;
}

static inline bool pkvm_arm_smmu_fw_dabt_handler(struct user_pt_regs *regs,
						 u64 esr, u64 addr)
{
	return false;
}
#endif

#endif /* __KVM_NVHE_ARM_SMMU_FW_H__ */
