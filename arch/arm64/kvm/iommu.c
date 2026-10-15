// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

static DEFINE_MUTEX(pkvm_iommu_reg_lock);

/**
 * pkvm_iommu_register_driver() - Register the hypervisor IOMMU driver
 * @hyp_ops: Kernel address of the ops, which live in the hypervisor image,
 *           i.e. &kvm_nvhe_sym(ops).
 * @nr_pages: Number of pages the driver needs from the IOMMU pool, see
 *            kvm-arm.iommu_pgt_mem.
 *
 * Only one driver can be registered. Call from an initcall between
 * core_initcall() and fs_initcall().
 *
 * Return: 0 on success.
 */
int __init pkvm_iommu_register_driver(struct pkvm_iommu_ops *hyp_ops,
				      unsigned int nr_pages)
{
	guard(mutex)(&pkvm_iommu_reg_lock);

	if (!is_protected_kvm_enabled() || is_kvm_arm_initialised())
		return -EPERM;

	if (kvm_nvhe_sym(pkvm_iommu_ops))
		return -EBUSY;

	if (!nr_pages)
		return -EINVAL;

	if (nr_pages > pkvm_iommu_pages()) {
		kvm_err("IOMMU pool needs kvm-arm.iommu_pgt_mem=%luK or more\n",
			nr_pages * PAGE_SIZE / SZ_1K);
		return -ENOMEM;
	}

	kvm_nvhe_sym(pkvm_iommu_ops) = kern_hyp_va(kvm_ksym_ref(hyp_ops));
	return 0;
}

static int __init early_iommu_pgt_mem(char *arg)
{
	unsigned long long requested_size;

	if (!arg)
		return -EINVAL;

	requested_size = memparse(arg, NULL);

	/* hyp_pool_init() computes nr_pages << PAGE_SHIFT in an unsigned int. */
	if (requested_size > (UINT_MAX & PAGE_MASK)) {
		kvm_err("kvm-arm.iommu_pgt_mem is too large\n");
		return -EINVAL;
	}

	kvm_nvhe_sym(hyp_iommu_pages) = DIV_ROUND_UP(requested_size, PAGE_SIZE);
	return 0;
}
early_param("kvm-arm.iommu_pgt_mem", early_iommu_pgt_mem);
