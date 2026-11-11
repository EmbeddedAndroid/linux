// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel side of the arm-smmu pKVM hypervisor driver: find the application
 * MMU-500, register the hypervisor driver before KVM initialises, keep the
 * host driver off the context bank the hypervisor takes, and report the
 * faults of the shadow stage 2.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cma.h>
#include <linux/debugfs.h>
#include <linux/dma-map-ops.h>
#include <linux/io.h>
#include <linux/kvm_host.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include "arm-smmu.h"
#include "arm-smmu-pkvm.h"

extern struct arm_smmu_pkvm_params kvm_nvhe_sym(arm_smmu_pkvm_params);
extern struct pkvm_iommu_ops kvm_nvhe_sym(arm_smmu_pkvm_ops);

static struct arm_smmu_pkvm_params smmu_pkvm;

/* Context banks the host driver may use on the SMMU at @base */
unsigned int arm_smmu_pkvm_host_cbs(phys_addr_t base, unsigned int nr_cb)
{
	if (!smmu_pkvm.base || base != smmu_pkvm.base)
		return nr_cb;
	return min(nr_cb, smmu_pkvm.nr_cb_host);
}

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
	struct cma *cma;
	u32 nr_cb;
	int ret;

	if (!is_protected_kvm_enabled())
		return 0;

	for_each_compatible_node(np, NULL, "arm,mmu-500") {
		if (of_device_is_compatible(np, "qcom,adreno-smmu") ||
		    !of_device_is_available(np))
			continue;
		if (p.base || of_address_to_resource(np, 0, &res)) {
			pr_err("arm-smmu-pkvm: %pOF: more than one application SMMU\n", np);
			of_node_put(np);
			return 0;
		}
		p.base = res.start;
	}
	if (!p.base)
		return 0;

	gr0 = ioremap(p.base, PAGE_SIZE);
	if (!gr0)
		return -ENOMEM;
	nr_cb = FIELD_GET(ARM_SMMU_ID1_NUMCB, readl_relaxed(gr0 + ARM_SMMU_GR0_ID1));
	iounmap(gr0);
	if (nr_cb < 2)
		return -ENODEV;
	p.nr_cb_host = nr_cb - 1;

	cma = dev_get_cma_area(NULL);
	if (cma) {
		p.pages_base = cma_get_base(cma);
		p.pages_size = cma_get_size(cma);
	}

	/*
	 * Level 1 and one level 2 table per GiB of the 36-bit space, one level
	 * 3 table per 2 MiB of the page-mapped range, and room for block splits.
	 */
	nr_pages = 1 + 64 + DIV_ROUND_UP(p.pages_size, SZ_2M) + 512;

	kvm_nvhe_sym(arm_smmu_pkvm_params) = p;
	ret = pkvm_iommu_register_driver(&kvm_nvhe_sym(arm_smmu_pkvm_ops), nr_pages);
	if (ret) {
		pr_err("arm-smmu-pkvm: hypervisor driver not registered: %d\n", ret);
		return ret;
	}

	smmu_pkvm = p;
	pr_info("arm-smmu-pkvm: SMMU %pa: %u context banks left to the host, CMA %pa+%#llx page mapped\n",
		&p.base, p.nr_cb_host, &p.pages_base, p.pages_size);
	return 0;
}
subsys_initcall(arm_smmu_pkvm_register);

static int arm_smmu_pkvm_show(struct seq_file *m, void *v)
{
	u64 fsr = kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_FSR);
	u64 far = kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_FAR);
	u64 synr = kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_SYNR);
	u64 tables = kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_TABLES);

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
