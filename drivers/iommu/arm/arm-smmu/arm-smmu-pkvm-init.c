// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel side of the arm-smmu pKVM hypervisor driver: find the application
 * MMU-500, register the hypervisor driver before KVM initialises, register
 * as the protected stream provider so that the host driver defers its probe
 * until pKVM is finalised, and report the faults of the shadow stage 2.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/dma-map-ops.h>
#include <linux/io.h>
#include <linux/iommu-prot.h>
#include <linux/kvm_host.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include "arm-smmu.h"
#include "arm-smmu-pkvm.h"

DECLARE_KVM_NVHE_SYM(arm_smmu_pkvm_params);
DECLARE_KVM_NVHE_SYM(arm_smmu_pkvm_ops);

static struct arm_smmu_pkvm_params smmu_pkvm;

/* The hypervisor programs the SMMU at init and traps it once finalised */
static bool arm_smmu_pkvm_may_probe(phys_addr_t base)
{
	return !smmu_pkvm.base || base != smmu_pkvm.base || is_pkvm_initialized();
}

static const struct iommu_prot_ops arm_smmu_pkvm_prot_ops = {
	.may_probe	= arm_smmu_pkvm_may_probe,
};

/*
 * The application SMMU is the one MMU-500 that is not a GPU SMMU. The
 * host still owns it here and reads how many context banks it has.
 */
static int __init arm_smmu_pkvm_register(void)
{
	struct arm_smmu_pkvm_params p = { };
	struct device_node *np;
	unsigned int nr_pages;
	struct resource res;
	void __iomem *gr0;
	u32 nr_cb, i;
	int ret = 0;

	if (!is_protected_kvm_enabled())
		return 0;

	/* Registered below in every case: no other may be */
	if (iommu_prot_get_ops()) {
		pr_err("arm-smmu-pkvm: another protected stream provider\n");
		return -EBUSY;
	}

	for_each_compatible_node(np, NULL, "arm,mmu-500") {
		if (of_device_is_compatible(np, "qcom,adreno-smmu") ||
		    !of_device_is_available(np))
			continue;
		if (p.base || of_address_to_resource(np, 0, &res)) {
			pr_err("arm-smmu-pkvm: %pOF: more than one application SMMU, or no address\n",
			       np);
			of_node_put(np);
			ret = -ENODEV;
			goto out;
		}
		p.base = res.start;
	}
	if (!p.base)
		return 0;

	gr0 = ioremap(p.base, PAGE_SIZE);
	if (!gr0) {
		ret = -ENOMEM;
		goto out;
	}
	nr_cb = FIELD_GET(ARM_SMMU_ID1_NUMCB, readl_relaxed(gr0 + ARM_SMMU_GR0_ID1));
	iounmap(gr0);
	if (nr_cb < 2) {
		ret = -ENODEV;
		goto out;
	}
	p.nr_cb_host = nr_cb - 1;

	/*
	 * Level 1 and one level 2 table per GiB of the 36-bit space, one level
	 * 3 table per 2 MiB of the hypervisor's memory list (all of it is page
	 * mapped, no-map regions included), and room for the splits of device
	 * blocks at init.
	 */
	nr_pages = 1 + 64 + 64;
	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		struct memblock_region *reg = &kvm_nvhe_sym(hyp_memory)[i];

		nr_pages += (ALIGN(reg->base + reg->size, SZ_2M) -
			     ALIGN_DOWN(reg->base, SZ_2M)) / SZ_2M;
	}

	*(struct arm_smmu_pkvm_params *)kvm_nvhe_sym(arm_smmu_pkvm_params) = p;
	ret = pkvm_iommu_register_driver((struct pkvm_iommu_ops *)kvm_nvhe_sym(arm_smmu_pkvm_ops),
					 nr_pages);
	if (!ret)
		smmu_pkvm = p;
out:
	/*
	 * Without the hypervisor driver the provider owns no stream: the SMMU
	 * stays the host's, and protected memory is refused to every device
	 * it translates.
	 */
	WARN_ON(iommu_prot_register(&arm_smmu_pkvm_prot_ops));
	if (ret) {
		pr_err("arm-smmu-pkvm: no DMA isolation (%d): protected memory refused to translated devices\n",
		       ret);
		return ret;
	}
	pr_info("arm-smmu-pkvm: SMMU %pa: %u context banks left to the host, %u table pages\n",
		&p.base, p.nr_cb_host, nr_pages);
	return 0;
}
subsys_initcall(arm_smmu_pkvm_register);

/* After finalize_pkvm(): without pKVM the SMMU is the host's again */
static int __init arm_smmu_pkvm_check(void)
{
	if (smmu_pkvm.base && !is_pkvm_initialized()) {
		pr_err("arm-smmu-pkvm: SMMU %pa: pKVM not initialised, left to the host\n",
		       &smmu_pkvm.base);
		smmu_pkvm.base = 0;
	}
	return 0;
}
device_initcall_sync(arm_smmu_pkvm_check);

static int arm_smmu_pkvm_show(struct seq_file *m, void *v)
{
	u64 fsr = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info, ARM_SMMU_PKVM_FSR);
	u64 far = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info, ARM_SMMU_PKVM_FAR);
	u64 synr = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info, ARM_SMMU_PKVM_SYNR);
	u64 tables = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info, ARM_SMMU_PKVM_TABLES);

	if ((s64)fsr < 0)
		return (int)fsr;
	seq_printf(m, "faults %llu fsr %#llx far %#llx fsynr0 %#llx cbfrsynra %#llx\n",
		   fsr >> 32, fsr & U32_MAX, far, synr & U32_MAX, synr >> 32);
	seq_printf(m, "tables %llu\n", tables);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(arm_smmu_pkvm);

static int __init arm_smmu_pkvm_debugfs(void)
{
	if (smmu_pkvm.base && is_protected_kvm_enabled())
		debugfs_create_file("arm_smmu_pkvm", 0400, NULL, NULL,
				    &arm_smmu_pkvm_fops);
	return 0;
}
late_initcall(arm_smmu_pkvm_debugfs);
