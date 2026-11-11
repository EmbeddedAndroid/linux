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
 * The secure context banks of the video codec, from the DT binding of its
 * context bank subnodes (qcom,venus-common.yaml): the stream ID and mask
 * from "iommus", the IOVA window from "iommu-ranges" or, without it, from
 * the codec's dma-ranges above its firmware window.
 */
static const char * const bank_nodes[] = {
	[ARM_SMMU_PKVM_BANK_BITSTREAM]	= "secure-bitstream",
	[ARM_SMMU_PKVM_BANK_PIXEL]	= "secure-pixel",
	[ARM_SMMU_PKVM_BANK_NON_PIXEL]	= "secure-non-pixel",
};

/* Downstream keeps secure pixel and bitstream IOVAs above the firmware */
#define BANK_IOVA_MIN		0x00500000

static int __init collect_banks(struct arm_smmu_pkvm_params *p)
{
	struct device_node *codec, *np;
	struct of_phandle_args args;
	struct resource res;
	u32 range[2];
	u64 dma_end;
	int i;

	codec = of_find_node_with_property(NULL, "dma-ranges");
	for (; codec; codec = of_find_node_with_property(codec, "dma-ranges"))
		if (of_get_child_by_name(codec, bank_nodes[0]))
			break;
	if (!codec)
		return 0;

	/* One entry: 1-cell child address, 2-cell parent address, 1-cell size */
	if (of_property_read_u32_index(codec, "dma-ranges", 3, &range[1])) {
		of_node_put(codec);
		return -EINVAL;
	}
	dma_end = range[1];

	for (i = 0; i < ARRAY_SIZE(bank_nodes); i++) {
		struct arm_smmu_pkvm_bank *b = &p->banks[i];

		np = of_get_child_by_name(codec, bank_nodes[i]);
		if (!np ||
		    of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0, &args) ||
		    args.args_count != 2 || of_address_to_resource(args.np, 0, &res) ||
		    res.start != p->base) {
			pr_err("arm-smmu-pkvm: %pOF: no %s bank\n", codec, bank_nodes[i]);
			of_node_put(np);
			of_node_put(codec);
			return -EINVAL;
		}
		of_node_put(args.np);
		b->smr = args.args[1] << 16 | args.args[0];
		if (!of_property_read_u32_array(np, "iommu-ranges", range, 2)) {
			b->iova_start = range[0];
			b->iova_end = range[0] + range[1];
		} else {
			b->iova_start = BANK_IOVA_MIN;
			b->iova_end = min_t(u64, dma_end, U32_MAX & PAGE_MASK);
		}
		of_node_put(np);
		pr_info("arm-smmu-pkvm: %s: SMR %#x, IOVA %#x-%#x to the hypervisor\n",
			bank_nodes[i], b->smr, b->iova_start, b->iova_end);
	}
	of_node_put(codec);
	p->nr_banks = ARRAY_SIZE(bank_nodes);
	return 0;
}

int arm_smmu_pkvm_prot_map(unsigned int bank, u32 iova, phys_addr_t phys, size_t size)
{
	if (!smmu_pkvm.base || !PAGE_ALIGNED(phys) || !PAGE_ALIGNED(size))
		return -EINVAL;
	return kvm_call_hyp_nvhe(__pkvm_arm_smmu_bank_map, bank, iova,
				 PHYS_PFN(phys), size >> PAGE_SHIFT);
}
EXPORT_SYMBOL_GPL(arm_smmu_pkvm_prot_map);

int arm_smmu_pkvm_prot_unmap(unsigned int bank, u32 iova, size_t size)
{
	if (!smmu_pkvm.base || !PAGE_ALIGNED(size))
		return -EINVAL;
	return kvm_call_hyp_nvhe(__pkvm_arm_smmu_bank_unmap, bank, iova,
				 size >> PAGE_SHIFT);
}
EXPORT_SYMBOL_GPL(arm_smmu_pkvm_prot_unmap);

bool arm_smmu_pkvm_prot_bank(unsigned int bank, u32 *iova_start, u32 *iova_end)
{
	if (bank >= smmu_pkvm.nr_banks)
		return false;
	*iova_start = smmu_pkvm.banks[bank].iova_start;
	*iova_end = smmu_pkvm.banks[bank].iova_end;
	return true;
}
EXPORT_SYMBOL_GPL(arm_smmu_pkvm_prot_bank);

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
	ret = collect_banks(&p);
	if (ret)
		return ret;
	if (nr_cb < 2 + p.nr_banks)
		return -ENODEV;
	p.nr_cb_host = nr_cb - 1 - p.nr_banks;

	cma = dev_get_cma_area(NULL);
	if (cma) {
		p.pages_base = cma_get_base(cma);
		p.pages_size = cma_get_size(cma);
	}

	/*
	 * Level 1 and one level 2 table per GiB of the 36-bit space, one level
	 * 3 table per 2 MiB of the page-mapped range, and room for block splits.
	 */
	nr_pages = 1 + 64 + DIV_ROUND_UP(p.pages_size, SZ_2M) + 512 +
		   p.nr_banks * 64;

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
	int i;

	if ((s64)fsr < 0)
		return (int)fsr;
	seq_printf(m, "faults %llu fsr %#llx far %#llx fsynr0 %#llx cbfrsynra %#llx\n",
		   fsr >> 32, fsr & U32_MAX, far, synr & U32_MAX, synr >> 32);
	seq_printf(m, "tables %llu\n", tables);
	for (i = 0; i < 2; i++)
		seq_printf(m, "probe %s pa %#llx sum %#llx\n", i ? "pool" : "data",
			   kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_PROBE + i),
			   kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault, ARM_SMMU_PKVM_PROBE_SUM + i));
	for (i = 0; i < smmu_pkvm.nr_banks; i++) {
		u64 f = kvm_call_hyp_nvhe(__pkvm_arm_smmu_fault,
					  ARM_SMMU_PKVM_BANK_FAULT + i);

		seq_printf(m, "%s fsr %#llx far %#llx\n", bank_nodes[i],
			   f >> 32, f & U32_MAX);
	}
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
