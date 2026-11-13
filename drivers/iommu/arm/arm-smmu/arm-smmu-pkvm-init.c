// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel side of the arm-smmu pKVM hypervisor driver: find the application
 * MMU-500, register the hypervisor driver before KVM initialises, register
 * as the protected stream provider (the host driver defers its probe until
 * pKVM is finalised, and maps of protected streams go to the hypervisor),
 * and report the faults of the shadow stage 2 and of the protected banks.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cma.h>
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

static u32 arm_smmu_pkvm_stream(u32 id, u32 mask)
{
	return FIELD_PREP(ARM_SMMU_SMR_MASK, mask) | FIELD_PREP(ARM_SMMU_SMR_ID, id);
}

static bool arm_smmu_pkvm_overlaps(u32 a, u32 b)
{
	u32 ignore = FIELD_GET(ARM_SMMU_SMR_MASK, a) | FIELD_GET(ARM_SMMU_SMR_MASK, b);

	return !((FIELD_GET(ARM_SMMU_SMR_ID, a) ^ FIELD_GET(ARM_SMMU_SMR_ID, b)) & ~ignore);
}

#define HYP_PARAMS	(*(struct arm_smmu_pkvm_params *)kvm_nvhe_sym(arm_smmu_pkvm_params))

/*
 * Streams are declared at boot, before KVM initialises the hypervisor
 * (device_initcall), which programs a bank for each from its parameters:
 * the kernel cannot name another stream once it is deprivileged.
 */
static int arm_smmu_pkvm_declare(phys_addr_t base, u32 id, u32 mask)
{
	struct arm_smmu_pkvm_params *p = &HYP_PARAMS;
	u32 smr = arm_smmu_pkvm_stream(id, mask);
	u32 i;

	if (!smmu_pkvm.base || base != smmu_pkvm.base)
		return -ENODEV;
	if (is_kvm_arm_initialised())
		return -EBUSY;
	/* The SMR mask has 15 bits; bit 31 is the valid bit */
	if (id > FIELD_MAX(ARM_SMMU_SMR_ID) || mask > GENMASK(14, 0))
		return -EINVAL;
	/* The hypervisor would refuse to start with streams that overlap */
	for (i = 0; i < p->nr_prot; i++)
		if (arm_smmu_pkvm_overlaps(p->prot_smr[i], smr))
			return -EEXIST;
	if (p->nr_prot >= ARM_SMMU_PKVM_PROT_BANKS)
		return -ENOSPC;
	p->prot_smr[p->nr_prot++] = smr;
	smmu_pkvm = *p;
	return 0;
}

static bool arm_smmu_pkvm_can_protect(phys_addr_t base, u32 id, u32 mask)
{
	u32 i;

	if (!smmu_pkvm.base || base != smmu_pkvm.base)
		return false;
	for (i = 0; i < smmu_pkvm.nr_prot; i++)
		if (smmu_pkvm.prot_smr[i] == arm_smmu_pkvm_stream(id, mask))
			return true;
	return false;
}

/* The hypervisor maps at most 2 MiB per call */
#define PROT_CHUNK	SZ_2M

static int arm_smmu_pkvm_unmap(phys_addr_t base, u32 id, u32 mask,
			       unsigned long iova, size_t size)
{
	size_t n;
	int ret;

	if (!arm_smmu_pkvm_can_protect(base, id, mask) || !PAGE_ALIGNED(size))
		return -EINVAL;
	for (; size; size -= n, iova += n) {
		n = min_t(size_t, size, PROT_CHUNK);
		ret = kvm_call_hyp_nvhe(__pkvm_iommu_prot_unmap,
					arm_smmu_pkvm_stream(id, mask), iova,
					n >> PAGE_SHIFT);
		if (ret)
			return ret;
	}
	return 0;
}

static int arm_smmu_pkvm_map(phys_addr_t base, u32 id, u32 mask,
			     unsigned long iova, phys_addr_t paddr, size_t size,
			     int prot)
{
	size_t done, n;
	int ret = 0;

	if (!arm_smmu_pkvm_can_protect(base, id, mask) ||
	    !PAGE_ALIGNED(paddr) || !PAGE_ALIGNED(size))
		return -EINVAL;
	for (done = 0; done < size; done += n) {
		n = min_t(size_t, size - done, PROT_CHUNK);
		ret = kvm_call_hyp_nvhe(__pkvm_iommu_prot_map,
					arm_smmu_pkvm_stream(id, mask), iova + done,
					PHYS_PFN(paddr + done), n >> PAGE_SHIFT,
					prot);
		if (ret)
			break;
	}
	/* A failure leaves nothing mapped */
	if (ret && done)
		WARN_ON(arm_smmu_pkvm_unmap(base, id, mask, iova, done));
	return ret;
}

static const struct iommu_prot_ops arm_smmu_pkvm_prot_ops = {
	.may_probe	= arm_smmu_pkvm_may_probe,
	.declare	= arm_smmu_pkvm_declare,
	.can_protect	= arm_smmu_pkvm_can_protect,
	.map		= arm_smmu_pkvm_map,
	.unmap		= arm_smmu_pkvm_unmap,
};

/*
 * The application SMMU is the one MMU-500 that is not a GPU SMMU. The
 * host still owns it here and reads how many context banks it has.
 */
static int __init arm_smmu_pkvm_register(void)
{
	struct arm_smmu_pkvm_params p = { };
	struct cma *cma = dev_get_cma_area(NULL);
	u32 nr_cb, nr_smr, i, busy = 0;
	struct device_node *np;
	unsigned int nr_pages;
	struct resource res;
	void __iomem *gr0;
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
	nr_smr = FIELD_GET(ARM_SMMU_ID0_NUMSMRG, readl_relaxed(gr0 + ARM_SMMU_GR0_ID0));
	/* The hypervisor takes the top stream match groups: they must be free */
	for (i = nr_smr - ARM_SMMU_PKVM_PROT_BANKS; i < nr_smr; i++)
		busy |= readl_relaxed(gr0 + ARM_SMMU_GR0_SMR(i)) & ARM_SMMU_SMR_VALID;
	iounmap(gr0);
	if (nr_cb < 2 + ARM_SMMU_PKVM_PROT_BANKS ||
	    nr_smr <= ARM_SMMU_PKVM_PROT_BANKS || busy) {
		pr_err("arm-smmu-pkvm: SMMU %pa: no room for the hypervisor\n", &p.base);
		ret = -ENODEV;
		goto out;
	}
	p.nr_cb_host = nr_cb - 1 - ARM_SMMU_PKVM_PROT_BANKS;

	/*
	 * A bank maps only lent memory, which the protected heaps take from
	 * CMA: its level 1, four level 2 for the 32-bit IOVA, one level 3 per
	 * 2 MiB of CMA and one per buffer that straddles 2 MiB.
	 */
	p.bank_tables = 1 + 4 + (cma ? DIV_ROUND_UP(cma_get_size(cma), SZ_2M) : 0) + 64;

	/*
	 * Level 1 and one level 2 table per GiB of the 36-bit space, one level
	 * 3 table per 2 MiB of the hypervisor's memory list (all of it is page
	 * mapped, no-map regions included), room for the splits of device
	 * blocks at init, and the tables of the protected banks.
	 */
	nr_pages = 1 + 64 + 64 + ARM_SMMU_PKVM_PROT_BANKS * p.bank_tables;
	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		struct memblock_region *reg = &kvm_nvhe_sym(hyp_memory)[i];

		nr_pages += (ALIGN(reg->base + reg->size, SZ_2M) -
			     ALIGN_DOWN(reg->base, SZ_2M)) / SZ_2M;
	}

	HYP_PARAMS = p;
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
	int i;

	if ((s64)fsr < 0)
		return (int)fsr;
	seq_printf(m, "faults %llu fsr %#llx far %#llx fsynr0 %#llx cbfrsynra %#llx\n",
		   fsr >> 32, fsr & U32_MAX, far, synr & U32_MAX, synr >> 32);
	seq_printf(m, "tables %llu\n", tables);
	for (i = 0; i < ARM_SMMU_PKVM_PROT_BANKS; i++) {
		u64 f = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info,
					  ARM_SMMU_PKVM_BANK_FAULT + i);
		u64 smr = kvm_call_hyp_nvhe(__pkvm_iommu_fault_info,
					    ARM_SMMU_PKVM_BANK_SMR + i);

		seq_printf(m, "bank %d smr %#llx fsr %#llx far %#llx\n", i,
			   smr, f >> 32, f & U32_MAX);
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
