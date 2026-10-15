// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>

static DEFINE_MUTEX(pkvm_iommu_reg_lock);

/**
 * pkvm_iommu_register_driver() - Register the hypervisor IOMMU driver
 * @hyp_ops: Kernel address of the ops, which live in the hypervisor image,
 *           i.e. &kvm_nvhe_sym(ops).
 *
 * Only one driver can be registered. Call from an initcall between
 * core_initcall() and fs_initcall().
 *
 * Return: 0 on success.
 */
int __init pkvm_iommu_register_driver(struct pkvm_iommu_ops *hyp_ops)
{
	guard(mutex)(&pkvm_iommu_reg_lock);

	if (!is_protected_kvm_enabled() || is_kvm_arm_initialised())
		return -EPERM;

	if (kvm_nvhe_sym(pkvm_iommu_ops))
		return -EBUSY;

	kvm_nvhe_sym(pkvm_iommu_ops) = kern_hyp_va(kvm_ksym_ref(hyp_ops));
	return 0;
}
