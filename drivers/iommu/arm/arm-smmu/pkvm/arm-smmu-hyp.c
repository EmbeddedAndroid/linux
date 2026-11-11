// SPDX-License-Identifier: GPL-2.0-only
/*
 * pKVM hypervisor driver for an Arm MMU-500 (SMMUv2) without the Security
 * Extensions: DMA isolation by trap and emulate, on the pKVM IOMMU core.
 *
 * The hypervisor keeps the top context bank of the SMMU for a stage-2 only
 * bank that shadows the host stage-2 (pkvm_iommu_ops.host_stage2_idmap),
 * and takes the global register pages GR0 and GR1 and that bank's page
 * away from the host. Host accesses to GR0 and GR1 are emulated so that
 * every host context bank translates through that stage 2: a stage-1 bank
 * becomes a nested bank, a bypass stream becomes a stream of the shadow
 * bank, a host stage-2 bank only gets fault streams, and translation
 * cannot be turned off. Memory that leaves the host (donated to the
 * hypervisor or a protected guest, lent over FF-A) leaves the shadow
 * before the transition completes, so no device a host driver programs
 * can reach it.
 *
 * Unlike SMMUv3, an MMU-500 has no command queue or stream table in
 * memory: the whole configuration is in GR0, GR1 and the context bank
 * pages, so the emulation is a filter on 32-bit register writes.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/iommu.h>

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include <nvhe/iommu.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

#include "../arm-smmu-pkvm.h"

struct arm_smmu_pkvm_params arm_smmu_pkvm_params;

#define SMMU_GR0_sCR0			0x0
#define SMMU_sCR0_VMID16EN		BIT(31)
#define SMMU_sCR0_USFCFG		BIT(10)
#define SMMU_sCR0_EXIDENABLE		BIT(3)
#define SMMU_sCR0_CLIENTPD		BIT(0)
#define SMMU_GR0_ID0			0x20
#define SMMU_ID0_NTS			BIT(28)
#define SMMU_ID0_EXIDS			BIT(8)
#define SMMU_ID0_NUMSMRG		GENMASK(7, 0)
#define SMMU_GR0_ID1			0x24
#define SMMU_ID1_PAGESIZE		BIT(31)
#define SMMU_ID1_NUMPAGENDXB		GENMASK(30, 28)
#define SMMU_ID1_NUMS2CB		GENMASK(23, 16)
#define SMMU_ID1_NUMCB			GENMASK(7, 0)
#define SMMU_GR0_ID2			0x28
#define SMMU_ID2_VMID16			BIT(15)
#define SMMU_GR0_TLBIVMID		0x64
#define SMMU_GR0_TLBIALLNSNH		0x68
#define SMMU_GR0_TLBGSYNC		0x70
#define SMMU_GR0_TLBGSTATUS		0x74
#define SMMU_TLBGSTATUS_GSACTIVE	BIT(0)
#define SMMU_GR0_SMR(n)			(0x800 + ((n) << 2))
#define SMMU_SMR_VALID			BIT(31)
#define SMMU_SMR_MASK			GENMASK(30, 16)
#define SMMU_SMR_ID			GENMASK(15, 0)
#define SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define SMMU_S2CR_TYPE			GENMASK(17, 16)
#define SMMU_S2CR_TYPE_TRANS		0
#define SMMU_S2CR_TYPE_BYPASS		1
#define SMMU_S2CR_TYPE_FAULT		2
#define SMMU_S2CR_CBNDX			GENMASK(7, 0)
#define SMMU_GR1_CBAR(n)		((n) << 2)
#define SMMU_CBAR_IRPTNDX		GENMASK(31, 24)
#define SMMU_CBAR_TYPE			GENMASK(17, 16)
#define SMMU_CBAR_TYPE_S2		0
#define SMMU_CBAR_TYPE_S1_BYPASS	1
#define SMMU_CBAR_TYPE_S1_S2		3
#define SMMU_CBAR_S2CBNDX		GENMASK(15, 8)
#define SMMU_CBAR_VMID			GENMASK(7, 0)
#define SMMU_GR1_CBFRSYNRA(n)		(0x400 + ((n) << 2))
#define SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define SMMU_CBA2R_VA64			BIT(0)
#define SMMU_CB_SCTLR			0x0
#define SMMU_SCTLR_M			BIT(0)
#define SMMU_SCTLR_TRE			BIT(1)
#define SMMU_SCTLR_AFE			BIT(2)
#define SMMU_SCTLR_CFRE			BIT(5)
#define SMMU_CB_TTBR0			0x20
#define SMMU_CB_TCR			0x30
#define SMMU_CB_FSR			0x58
/* TF, AFF, PF, EF, TLBMCF, TLBLKF, ASF, UUT, SS, MULTI: not FORMAT */
#define SMMU_FSR_FAULT			(GENMASK(8, 1) | BIT(30) | BIT(31))
#define SMMU_CB_FAR			0x60
#define SMMU_CB_FSYNR0			0x68
#define SMMU_MAX_ENTRIES		256

/* The hypervisor's VMIDs: host stage-2 banks get the ones below */
#define HOST_VMIDS			0xfc
#define GUARD_VMID			0xff
#define BANK_VMID(b)			(HOST_VMIDS + (b))

/* Bank TCR: as GUARD_TCR, with a 32-bit IOVA (T0SZ 32) */
#define BANK_TCR	((GUARD_TCR & ~GENMASK(5, 0)) | 32)

/* VTCR format: 36-bit IPA and PA, 4 KiB granule, walk from level 1, WBWA IS */
#define GUARD_TCR	(BIT(31) | FIELD_PREP(GENMASK(18, 16), 1) |		\
			 FIELD_PREP(GENMASK(13, 12), 3) |			\
			 FIELD_PREP(GENMASK(11, 10), 1) |			\
			 FIELD_PREP(GENMASK(9, 8), 1) |				\
			 FIELD_PREP(GENMASK(7, 6), 1) | 28)
#define GUARD_IPA_BITS			36

/*
 * Stage-2 descriptor fields. MemAttr uses the non-FWB encoding the SMMU
 * walks with: 0xf Normal write-back, 0x1 Device-nGnRE.
 */
#define S2_AF				BIT(10)
#define S2_SH_IS			(3ULL << 8)
#define S2_AP_R				BIT(6)
#define S2_AP_W				BIT(7)
#define S2_MEMATTR_NORMAL		(0xfULL << 2)
#define S2_MEMATTR_DEVICE		(0x1ULL << 2)
#define S2_ATTR_MASK			(GENMASK_ULL(10, 2))
#define S2_BLOCK			1ULL
#define S2_PAGE				3ULL
#define S2_TABLE			3ULL
#define S2_TYPE_MASK			3ULL
#define S2_OA_MASK			GENMASK_ULL(47, 12)
#define PTRS				(PAGE_SIZE / sizeof(u64))



struct smmu_hyp {
	void __iomem		*gr0;
	void __iomem		*gr1;
	void __iomem		*cb;
	u32			cb_shadow;	/* the bank shadowing the host stage-2 */
	u32			nr_smr;
	u32			nr_cb;
	u32			cbar[SMMU_MAX_ENTRIES];	/* as the host wrote them */
	u32			s2cr[SMMU_MAX_ENTRIES];
	hyp_spinlock_t		lock;
	bool			ready;
	u32			fsr, fsynr0, cbfrsynra, nr_faults;
	u64			far;
	u32			nr_tables;
	u64			*l1;
	u32			cb_host;	/* context banks left to the host */
	u32			smr_host;	/* stream match groups left to the host */
	u32			nr_banks;
	void __iomem		*bank_cb[ARM_SMMU_PKVM_MAX_BANKS];
	u64			*bank_l1[ARM_SMMU_PKVM_MAX_BANKS];
};

static struct smmu_hyp smmu;

static void flush_pte(u64 *ptep)
{
	kvm_flush_dcache_to_poc(ptep, sizeof(*ptep));
}

static void flush_table(u64 *table)
{
	kvm_flush_dcache_to_poc(table, PAGE_SIZE);
}

static int smmu_tlb_sync(void)
{
	int timeout;

	writel(0, smmu.gr0 + SMMU_GR0_TLBGSYNC);
	for (timeout = 1000000; timeout; timeout--)
		if (!(readl_relaxed(smmu.gr0 + SMMU_GR0_TLBGSTATUS) &
		      SMMU_TLBGSTATUS_GSACTIVE))
			return 0;
	return -ETIMEDOUT;
}

static void smmu_tlb_flush(void)
{
	dsb(ishst);
	writel_relaxed(GUARD_VMID, smmu.gr0 + SMMU_GR0_TLBIVMID);
	WARN_ON(smmu_tlb_sync());
}

static u64 *table_va(u64 desc)
{
	return hyp_phys_to_virt(desc & S2_OA_MASK);
}

static u64 *new_table(void)
{
	u64 *t = pkvm_iommu_alloc_pages(0);

	if (t) {
		smmu.nr_tables++;
		flush_table(t);
	}
	return t;
}

static u64 s2_attrs(int prot)
{
	u64 attrs = S2_AF;

	if (prot & IOMMU_READ)
		attrs |= S2_AP_R;
	if (prot & IOMMU_WRITE)
		attrs |= S2_AP_W;
	if (prot & IOMMU_MMIO)
		attrs |= S2_MEMATTR_DEVICE;
	else
		attrs |= S2_MEMATTR_NORMAL | S2_SH_IS;
	return attrs;
}

static bool in_page_range(u64 addr, u64 end)
{
	struct arm_smmu_pkvm_params *p = &arm_smmu_pkvm_params;

	return addr < p->pages_base + p->pages_size && end > p->pages_base;
}

static void smmu_set_pte(u64 *ptep, u64 new, bool *flush)
{
	u64 old = READ_ONCE(*ptep);

	if (old == new)
		return;
	WRITE_ONCE(*ptep, new);
	flush_pte(ptep);
	if (old & 1)
		*flush = true;
}

/* Replace a valid block by a table of the same output, break before make. */
static int split_block(u64 *ptep)
{
	u64 old = READ_ONCE(*ptep), oa = old & S2_OA_MASK;
	u64 *t = new_table();
	int i;

	if (!t)
		return -ENOMEM;
	for (i = 0; i < PTRS; i++)
		t[i] = (oa + i * PAGE_SIZE) | (old & S2_ATTR_MASK) | S2_PAGE;
	flush_table(t);

	WRITE_ONCE(*ptep, 0);
	flush_pte(ptep);
	if (smmu.ready)
		smmu_tlb_flush();
	WRITE_ONCE(*ptep, hyp_virt_to_phys(t) | S2_TABLE);
	flush_pte(ptep);
	return 0;
}

static u64 *walk_l2(u64 addr, bool alloc)
{
	u64 *l1e = &smmu.l1[addr >> 30];
	u64 *t;

	if ((*l1e & S2_TYPE_MASK) == S2_TABLE)
		return &table_va(*l1e)[(addr >> 21) & (PTRS - 1)];
	if (!alloc)
		return NULL;

	/* Level 1 entries are always tables */
	t = new_table();
	if (!t)
		return NULL;
	WRITE_ONCE(*l1e, hyp_virt_to_phys(t) | S2_TABLE);
	flush_pte(l1e);
	return &t[(addr >> 21) & (PTRS - 1)];
}

/*
 * Identity map [addr, end) with @prot, 0 unmaps. 2 MiB blocks where the
 * range allows it, except in the range that is lent page by page (the
 * default CMA area, where the TEE protected heaps allocate), so that no
 * block there has to be split while devices use it.
 */
static int smmu_idmap(u64 addr, u64 end, int prot, bool *flush)
{
	u64 attrs = prot ? s2_attrs(prot) : 0;
	int ret;

	end = min_t(u64, end, BIT(GUARD_IPA_BITS));
	while (addr < end) {
		u64 next = min(ALIGN(addr + 1, SZ_2M), end);
		u64 *l2e = walk_l2(addr, prot), *l3e;

		if (!l2e) {
			if (prot)
				return -ENOMEM;
			addr = next;
			continue;
		}

		if (IS_ALIGNED(addr, SZ_2M) && next - addr == SZ_2M &&
		    !in_page_range(addr, next) &&
		    (*l2e & S2_TYPE_MASK) != S2_TABLE) {
			smmu_set_pte(l2e, prot ? addr | attrs | S2_BLOCK : 0, flush);
			addr = next;
			continue;
		}

		if ((*l2e & S2_TYPE_MASK) == S2_BLOCK) {
			if ((*l2e & S2_ATTR_MASK) == attrs)
				goto next_block;
			ret = split_block(l2e);
			if (ret)
				return ret;
		} else if (!(*l2e & 1)) {
			if (!prot)
				goto next_block;
			l3e = new_table();
			if (!l3e)
				return -ENOMEM;
			WRITE_ONCE(*l2e, hyp_virt_to_phys(l3e) | S2_TABLE);
			flush_pte(l2e);
		}

		for (; addr < next; addr += PAGE_SIZE) {
			l3e = &table_va(*l2e)[(addr >> PAGE_SHIFT) & (PTRS - 1)];
			smmu_set_pte(l3e, prot ? addr | attrs | S2_PAGE : 0, flush);
		}
		continue;
next_block:
		addr = next;
	}
	return 0;
}

/*
 * Protected banks: stage-2 only tables from a 32-bit IOVA to memory the
 * host lent to the secure world, page by page.
 */
static u64 *bank_pte(u32 b, u32 iova, bool alloc)
{
	u64 *t = smmu.bank_l1[b], *e;
	int lvl;

	for (lvl = 1; lvl < 3; lvl++) {
		e = &t[(iova >> (39 - 9 * lvl)) & (PTRS - 1)];
		if ((*e & S2_TYPE_MASK) != S2_TABLE) {
			if (!alloc)
				return NULL;
			t = new_table();
			if (!t)
				return NULL;
			WRITE_ONCE(*e, hyp_virt_to_phys(t) | S2_TABLE);
			flush_pte(e);
		}
		t = table_va(*e);
	}
	return &t[(iova >> PAGE_SHIFT) & (PTRS - 1)];
}

static void bank_tlb_flush(u32 b)
{
	dsb(ishst);
	writel_relaxed(BANK_VMID(b), smmu.gr0 + SMMU_GR0_TLBIVMID);
	WARN_ON(smmu_tlb_sync());
}

static bool bank_range_ok(u32 b, u32 iova, u32 nr_pages)
{
	struct arm_smmu_pkvm_bank *bank = &arm_smmu_pkvm_params.banks[b];
	u64 end = (u64)iova + (u64)nr_pages * PAGE_SIZE;

	return b < smmu.nr_banks && nr_pages && PAGE_ALIGNED(iova) &&
	       iova >= bank->iova_start && end <= bank->iova_end;
}

int arm_smmu_pkvm_bank_map(u32 bank, u32 iova, u64 pfn, u32 nr_pages)
{
	u64 phys = hyp_pfn_to_phys(pfn), size = (u64)nr_pages * PAGE_SIZE;
	u64 attrs = s2_attrs(IOMMU_READ | IOMMU_WRITE);
	int ret = 0;
	u32 i;

	if (!smmu.ready || !bank_range_ok(bank, iova, nr_pages))
		return -EINVAL;

	/*
	 * The host lock serialises bank updates; the lent state update below
	 * goes through the shadow stage-2 and takes smmu.lock itself.
	 */
	hyp_spin_lock(&host_mmu.lock);
	hyp_spin_lock(&smmu.lock);
	for (i = 0; i < nr_pages; i++) {
		u64 *e = bank_pte(bank, iova + i * PAGE_SIZE, false);

		if (e && (*e & 1)) {
			ret = -EEXIST;
			break;
		}
	}
	hyp_spin_unlock(&smmu.lock);
	/* Only memory the host lent, and not already in a bank */
	if (!ret)
		ret = __pkvm_host_ffa_lent_set_mapped(phys, size, true);
	if (ret)
		goto out_host;
	hyp_spin_lock(&smmu.lock);
	for (i = 0; i < nr_pages; i++) {
		u64 *e = bank_pte(bank, iova + i * PAGE_SIZE, true);

		if (!e) {
			ret = -ENOMEM;
			break;
		}
		WRITE_ONCE(*e, (phys + i * PAGE_SIZE) | attrs | S2_PAGE);
		flush_pte(e);
	}
	if (ret) {
		/* Out of table pages: undo, so that the host can reclaim */
		while (i--) {
			u64 *e = bank_pte(bank, iova + i * PAGE_SIZE, false);

			WRITE_ONCE(*e, 0);
			flush_pte(e);
		}
		bank_tlb_flush(bank);
	}
	hyp_spin_unlock(&smmu.lock);
	if (ret)
		WARN_ON(__pkvm_host_ffa_lent_set_mapped(phys, size, false));
out_host:
	hyp_spin_unlock(&host_mmu.lock);
	return ret;
}

int arm_smmu_pkvm_bank_unmap(u32 bank, u32 iova, u32 nr_pages)
{
	u64 phys = 0, *e;
	int ret = 0;
	u32 i;

	if (!smmu.ready || !bank_range_ok(bank, iova, nr_pages))
		return -EINVAL;

	hyp_spin_lock(&host_mmu.lock);
	hyp_spin_lock(&smmu.lock);
	/* A range comes back as it was mapped: contiguous in memory */
	for (i = 0; i < nr_pages; i++) {
		e = bank_pte(bank, iova + i * PAGE_SIZE, false);
		if (!e || !(*e & 1) ||
		    (i && (*e & S2_OA_MASK) != phys + i * PAGE_SIZE)) {
			ret = -ENOENT;
			goto out;
		}
		if (!i)
			phys = *e & S2_OA_MASK;
	}
	for (i = 0; i < nr_pages; i++) {
		e = bank_pte(bank, iova + i * PAGE_SIZE, false);
		WRITE_ONCE(*e, 0);
		flush_pte(e);
	}
	bank_tlb_flush(bank);
	hyp_spin_unlock(&smmu.lock);
	ret = __pkvm_host_ffa_lent_set_mapped(phys, (u64)nr_pages * PAGE_SIZE, false);
	WARN_ON(ret);
	hyp_spin_unlock(&host_mmu.lock);
	return ret;
out:
	hyp_spin_unlock(&smmu.lock);
	hyp_spin_unlock(&host_mmu.lock);
	return ret;
}

static int smmu_host_stage2_idmap(phys_addr_t start, phys_addr_t end, int prot)
{
	bool flush = false;
	int ret;

	hyp_spin_lock(&smmu.lock);
	ret = smmu_idmap(start, end, prot, &flush);
	if (flush && smmu.ready)
		smmu_tlb_flush();
	hyp_spin_unlock(&smmu.lock);
	return ret;
}

static bool host_cb(u32 n)
{
	return n < smmu.cb_host;
}

static bool host_smr(u32 n)
{
	return n < smmu.smr_host;
}

static bool smr_overlaps(u32 a, u32 b)
{
	u32 ignore = FIELD_GET(SMMU_SMR_MASK, a) | FIELD_GET(SMMU_SMR_MASK, b);

	return !((FIELD_GET(SMMU_SMR_ID, a) ^ FIELD_GET(SMMU_SMR_ID, b)) & ~ignore);
}

/* A host stream match that would catch a protected stream */
static bool smr_is_protected(u32 smr)
{
	unsigned int b;

	for (b = 0; b < smmu.nr_banks; b++)
		if (smr_overlaps(smr, arm_smmu_pkvm_params.banks[b].smr))
			return true;
	return false;
}

static u32 nest_cbar(u32 val)
{
	switch (FIELD_GET(SMMU_CBAR_TYPE, val)) {
	case SMMU_CBAR_TYPE_S1_BYPASS:
	case SMMU_CBAR_TYPE_S1_S2:
		return (val & SMMU_CBAR_IRPTNDX) |
		       FIELD_PREP(SMMU_CBAR_TYPE, SMMU_CBAR_TYPE_S1_S2) |
		       FIELD_PREP(SMMU_CBAR_S2CBNDX, smmu.cb_shadow);
	default:
		/*
		 * A host stage-2 bank cannot be nested: its streams fault. Its
		 * VMID must not alias the hypervisor's banks in the TLBs.
		 */
		return u32_replace_bits(val, FIELD_GET(SMMU_CBAR_VMID, val) % HOST_VMIDS,
					SMMU_CBAR_VMID);
	}
}

static u32 shadow_s2cr(u32 val)
{
	u32 cb = FIELD_GET(SMMU_S2CR_CBNDX, val);

	switch (FIELD_GET(SMMU_S2CR_TYPE, val)) {
	case SMMU_S2CR_TYPE_BYPASS:
		return FIELD_PREP(SMMU_S2CR_TYPE, SMMU_S2CR_TYPE_TRANS) |
		       FIELD_PREP(SMMU_S2CR_CBNDX, smmu.cb_shadow);
	case SMMU_S2CR_TYPE_TRANS:
		if (host_cb(cb) &&
		    FIELD_GET(SMMU_CBAR_TYPE, smmu.cbar[cb]) != SMMU_CBAR_TYPE_S2)
			return val;
		fallthrough;
	default:
		return FIELD_PREP(SMMU_S2CR_TYPE, SMMU_S2CR_TYPE_FAULT);
	}
}

static u32 gr0_read(u32 off)
{
	u32 val;

	if (off >= SMMU_GR0_S2CR(0) && off < SMMU_GR0_S2CR(SMMU_MAX_ENTRIES)) {
		u32 n = (off - SMMU_GR0_S2CR(0)) >> 2;

		return host_smr(n) ? smmu.s2cr[n] : 0;
	}

	if (off >= SMMU_GR0_SMR(smmu.smr_host) && off < SMMU_GR0_SMR(SMMU_MAX_ENTRIES))
		return 0;

	val = readl_relaxed(smmu.gr0 + off);
	switch (off) {
	case SMMU_GR0_ID0:
		val = u32_replace_bits(val, smmu.smr_host, SMMU_ID0_NUMSMRG);
		break;
	case SMMU_GR0_ID1:
		val = u32_replace_bits(val, smmu.cb_host, SMMU_ID1_NUMCB);
		val = u32_replace_bits(val, min_t(u32, smmu.cb_host,
						  FIELD_GET(SMMU_ID1_NUMS2CB, val)),
				       SMMU_ID1_NUMS2CB);
		break;
	case SMMU_GR0_ID2:
		val &= ~SMMU_ID2_VMID16;
		break;
	}
	return val;
}

static void gr0_write(u32 off, u32 val)
{
	if (off == SMMU_GR0_sCR0) {
		val &= ~(SMMU_sCR0_CLIENTPD | SMMU_sCR0_VMID16EN |
			 SMMU_sCR0_EXIDENABLE);
		val |= SMMU_sCR0_USFCFG;
	} else if (off >= SMMU_GR0_SMR(0) && off < SMMU_GR0_SMR(SMMU_MAX_ENTRIES)) {
		if (!host_smr((off - SMMU_GR0_SMR(0)) >> 2))
			return;
		if ((val & SMMU_SMR_VALID) && smr_is_protected(val))
			val &= ~SMMU_SMR_VALID;
	} else if (off >= SMMU_GR0_S2CR(0) &&
		   off < SMMU_GR0_S2CR(SMMU_MAX_ENTRIES)) {
		u32 n = (off - SMMU_GR0_S2CR(0)) >> 2;

		if (!host_smr(n))
			return;
		smmu.s2cr[n] = val;
		val = shadow_s2cr(val);
	}

	writel_relaxed(val, smmu.gr0 + off);
}

static u32 gr1_read(u32 off)
{
	u32 n = (off & (SZ_1K - 1)) >> 2;

	if (off < SMMU_GR1_CBA2R(SMMU_MAX_ENTRIES) && !host_cb(n))
		return 0;
	if (off < SMMU_GR1_CBFRSYNRA(0))
		return smmu.cbar[n];
	return readl_relaxed(smmu.gr1 + off);
}

static void gr1_write(u32 off, u32 val)
{
	u32 n = (off & (SZ_1K - 1)) >> 2;
	unsigned int i;
	bool was_s2;

	if (off < SMMU_GR1_CBA2R(SMMU_MAX_ENTRIES) && !host_cb(n))
		return;

	if (off >= SMMU_GR1_CBFRSYNRA(0)) {
		writel_relaxed(val, smmu.gr1 + off);
		return;
	}

	was_s2 = FIELD_GET(SMMU_CBAR_TYPE, smmu.cbar[n]) == SMMU_CBAR_TYPE_S2;
	smmu.cbar[n] = val;
	writel_relaxed(nest_cbar(val), smmu.gr1 + off);
	/* Streams of a bank that changed kind must follow it */
	if (was_s2 != (FIELD_GET(SMMU_CBAR_TYPE, val) == SMMU_CBAR_TYPE_S2))
		for (i = 0; i < smmu.smr_host; i++)
			if (FIELD_GET(SMMU_S2CR_CBNDX, smmu.s2cr[i]) == n)
				writel_relaxed(shadow_s2cr(smmu.s2cr[i]),
					       smmu.gr0 + SMMU_GR0_S2CR(i));
}

static bool smmu_dabt_handler(struct user_pt_regs *regs, u64 esr, u64 addr)
{
	bool write = esr & ESR_ELx_WNR;
	u32 rt = FIELD_GET(ESR_ELx_SRT_MASK, esr);
	u64 off = addr - arm_smmu_pkvm_params.base, val;

	if (!smmu.ready || off >= 2 * PAGE_SIZE)
		return false;

	/* The driver only makes 32-bit accesses to the global registers */
	if (FIELD_GET(ESR_ELx_SAS, esr) != 2 || (off & 3))
		return false;

	hyp_spin_lock(&smmu.lock);
	if (write) {
		val = rt == 31 ? 0 : regs->regs[rt];
		if (off < PAGE_SIZE)
			gr0_write(off, val);
		else
			gr1_write(off - PAGE_SIZE, val);
	} else {
		if (off < PAGE_SIZE)
			val = gr0_read(off);
		else
			val = gr1_read(off - PAGE_SIZE);
		if (esr & ESR_ELx_SSE)
			val = (u64)(s64)(s32)val;
		if (!(esr & ESR_ELx_SF))
			val = lower_32_bits(val);
		if (rt != 31)
			regs->regs[rt] = val;
	}
	hyp_spin_unlock(&smmu.lock);

	return true;
}

/*
 * The last fault of the shadow bank: a host-programmed DMA that reached
 * memory the host does not own. Read and cleared by the host for its
 * reports and tests.
 */
u64 arm_smmu_pkvm_fault(u32 field)
{
	u64 ret;
	u32 fsr;

	if (!smmu.ready)
		return -ENODEV;

	hyp_spin_lock(&smmu.lock);
	fsr = readl_relaxed(smmu.cb + SMMU_CB_FSR);
	if (fsr & SMMU_FSR_FAULT) {
		smmu.fsr = fsr;
		smmu.far = readq_relaxed(smmu.cb + SMMU_CB_FAR);
		smmu.fsynr0 = readl_relaxed(smmu.cb + SMMU_CB_FSYNR0);
		smmu.cbfrsynra = readl_relaxed(smmu.gr1 +
					       SMMU_GR1_CBFRSYNRA(smmu.cb_shadow));
		smmu.nr_faults++;
		writel_relaxed(fsr, smmu.cb + SMMU_CB_FSR);
	}
	switch (field) {
	case ARM_SMMU_PKVM_FSR:
		ret = (u64)smmu.nr_faults << 32 | smmu.fsr;
		break;
	case ARM_SMMU_PKVM_FAR:
		ret = smmu.far;
		break;
	case ARM_SMMU_PKVM_SYNR:
		ret = (u64)smmu.cbfrsynra << 32 | smmu.fsynr0;
		break;
	case ARM_SMMU_PKVM_TABLES:
		ret = smmu.nr_tables;
		break;
	default:
		if (field - ARM_SMMU_PKVM_BANK_FAULT < smmu.nr_banks) {
			void __iomem *cb = smmu.bank_cb[field - ARM_SMMU_PKVM_BANK_FAULT];

			fsr = readl_relaxed(cb + SMMU_CB_FSR);
			ret = (u64)(fsr & SMMU_FSR_FAULT) << 32 |
			      lower_32_bits(readq_relaxed(cb + SMMU_CB_FAR));
			writel_relaxed(fsr, cb + SMMU_CB_FSR);
		} else {
			ret = -EINVAL;
		}
	}
	hyp_spin_unlock(&smmu.lock);
	return ret;
}

static int donate_mmio(phys_addr_t pa, u64 nr_pages, void __iomem **va)
{
	unsigned long haddr;
	int ret;

	ret = __pkvm_host_donate_hyp_mmio(hyp_phys_to_pfn(pa), nr_pages, &haddr);
	if (!ret)
		*va = (void __iomem *)haddr;
	return ret;
}

/*
 * Map all memory and the MMIO below it before the host banks are nested,
 * so that DMA keeps working while the core then shadows the host stage-2:
 * pkvm_iommu_init() calls host_stage2_idmap for every range after init,
 * and unmaps what the host does not own (the hypervisor, its pools, this
 * driver's tables and the SMMU pages it took).
 */
static int smmu_initial_map(void)
{
	bool flush = false;
	u64 start = 0;
	int i, ret;

	for (i = 0; i < hyp_memblock_nr; i++) {
		struct memblock_region *reg = &hyp_memory[i];

		if (reg->base + reg->size > BIT(GUARD_IPA_BITS))
			return -E2BIG;
		if (start < reg->base) {
			ret = smmu_idmap(start, reg->base,
					 IOMMU_READ | IOMMU_WRITE | IOMMU_MMIO, &flush);
			if (ret)
				return ret;
		}
		ret = smmu_idmap(reg->base, reg->base + reg->size,
				 IOMMU_READ | IOMMU_WRITE, &flush);
		if (ret)
			return ret;
		start = reg->base + reg->size;
	}
	return 0;
}

static int smmu_init(void)
{
	struct arm_smmu_pkvm_params *p = &arm_smmu_pkvm_params;
	u32 id0, id1, numpage, cbar;
	unsigned int i;
	int ret;

	if (!p->base || !PAGE_ALIGNED(p->base) || PAGE_SIZE != SZ_4K)
		return -EINVAL;

	ret = donate_mmio(p->base, 2, &smmu.gr0);
	if (ret)
		return ret;
	smmu.gr1 = smmu.gr0 + PAGE_SIZE;

	id0 = readl_relaxed(smmu.gr0 + SMMU_GR0_ID0);
	id1 = readl_relaxed(smmu.gr0 + SMMU_GR0_ID1);
	smmu.nr_smr = FIELD_GET(SMMU_ID0_NUMSMRG, id0);
	smmu.nr_cb = FIELD_GET(SMMU_ID1_NUMCB, id1);
	numpage = 1 << (FIELD_GET(SMMU_ID1_NUMPAGENDXB, id1) + 1);
	smmu.nr_banks = p->nr_banks;
	if ((id1 & SMMU_ID1_PAGESIZE) || (id0 & SMMU_ID0_EXIDS) ||
	    !(id0 & SMMU_ID0_NTS) || smmu.nr_banks > ARM_SMMU_PKVM_MAX_BANKS ||
	    smmu.nr_cb < 2 + smmu.nr_banks || smmu.nr_smr <= smmu.nr_banks ||
	    smmu.nr_cb != p->nr_cb_host + 1 + smmu.nr_banks)
		return -EINVAL;
	smmu.cb_shadow = smmu.nr_cb - 1;
	smmu.cb_host = p->nr_cb_host;
	smmu.smr_host = smmu.nr_smr - smmu.nr_banks;

	ret = donate_mmio(p->base + (u64)(numpage + smmu.cb_host) * PAGE_SIZE,
			  smmu.nr_banks + 1, &smmu.cb);
	if (ret)
		return ret;
	for (i = 0; i < smmu.nr_banks; i++) {
		smmu.bank_cb[i] = smmu.cb + i * PAGE_SIZE;
		smmu.bank_l1[i] = new_table();
		if (!smmu.bank_l1[i])
			return -ENOMEM;
	}
	smmu.cb += smmu.nr_banks * PAGE_SIZE;

	smmu.l1 = new_table();
	if (!smmu.l1)
		return -ENOMEM;
	ret = smmu_initial_map();
	if (ret)
		return ret;
	flush_table(smmu.l1);

	hyp_spin_lock_init(&smmu.lock);

	/* Shadow bank: faults abort the transaction and are not signalled */
	writel_relaxed(0, smmu.cb + SMMU_CB_SCTLR);
	writel_relaxed(SMMU_CBA2R_VA64, smmu.gr1 + SMMU_GR1_CBA2R(smmu.cb_shadow));
	writel_relaxed(FIELD_PREP(SMMU_CBAR_TYPE, SMMU_CBAR_TYPE_S2) | GUARD_VMID,
		       smmu.gr1 + SMMU_GR1_CBAR(smmu.cb_shadow));
	writeq_relaxed(hyp_virt_to_phys(smmu.l1), smmu.cb + SMMU_CB_TTBR0);
	writel_relaxed(GUARD_TCR, smmu.cb + SMMU_CB_TCR);
	writel_relaxed(~0U, smmu.cb + SMMU_CB_FSR);
	wmb();
	writel_relaxed(SMMU_SCTLR_CFRE | SMMU_SCTLR_AFE | SMMU_SCTLR_TRE |
		       SMMU_SCTLR_M, smmu.cb + SMMU_CB_SCTLR);

	/* Protected banks: stage 2 only, empty until the host maps lent memory */
	for (i = 0; i < smmu.nr_banks; i++) {
		u32 cbn = smmu.cb_host + i, smr = smmu.smr_host + i;

		writel_relaxed(0, smmu.bank_cb[i] + SMMU_CB_SCTLR);
		writel_relaxed(SMMU_CBA2R_VA64, smmu.gr1 + SMMU_GR1_CBA2R(cbn));
		writel_relaxed(FIELD_PREP(SMMU_CBAR_TYPE, SMMU_CBAR_TYPE_S2) |
			       BANK_VMID(i), smmu.gr1 + SMMU_GR1_CBAR(cbn));
		writeq_relaxed(hyp_virt_to_phys(smmu.bank_l1[i]),
			       smmu.bank_cb[i] + SMMU_CB_TTBR0);
		writel_relaxed(BANK_TCR, smmu.bank_cb[i] + SMMU_CB_TCR);
		writel_relaxed(~0U, smmu.bank_cb[i] + SMMU_CB_FSR);
		wmb();
		writel_relaxed(SMMU_SCTLR_CFRE | SMMU_SCTLR_AFE | SMMU_SCTLR_TRE |
			       SMMU_SCTLR_M, smmu.bank_cb[i] + SMMU_CB_SCTLR);
		writel_relaxed(FIELD_PREP(SMMU_S2CR_CBNDX, cbn),
			       smmu.gr0 + SMMU_GR0_S2CR(smr));
		writel_relaxed(SMMU_SMR_VALID | p->banks[i].smr,
			       smmu.gr0 + SMMU_GR0_SMR(smr));
	}

	/*
	 * The host driver probed before pKVM was finalised: take over what it
	 * programmed.
	 */
	for (i = 0; i < smmu.cb_host; i++) {
		cbar = readl_relaxed(smmu.gr1 + SMMU_GR1_CBAR(i));
		smmu.cbar[i] = cbar;
		writel_relaxed(nest_cbar(cbar), smmu.gr1 + SMMU_GR1_CBAR(i));
	}
	for (i = 0; i < smmu.smr_host; i++) {
		u32 smr = readl_relaxed(smmu.gr0 + SMMU_GR0_SMR(i));

		if ((smr & SMMU_SMR_VALID) && smr_is_protected(smr))
			writel_relaxed(smr & ~SMMU_SMR_VALID, smmu.gr0 + SMMU_GR0_SMR(i));
		smmu.s2cr[i] = readl_relaxed(smmu.gr0 + SMMU_GR0_S2CR(i));
		writel_relaxed(shadow_s2cr(smmu.s2cr[i]),
			       smmu.gr0 + SMMU_GR0_S2CR(i));
	}
	gr0_write(SMMU_GR0_sCR0, readl_relaxed(smmu.gr0 + SMMU_GR0_sCR0));

	writel_relaxed(0, smmu.gr0 + SMMU_GR0_TLBIALLNSNH);
	ret = smmu_tlb_sync();
	if (ret)
		return ret;

	smmu.ready = true;
	return 0;
}

struct pkvm_iommu_ops arm_smmu_pkvm_ops = {
	.init			= smmu_init,
	.host_stage2_idmap	= smmu_host_stage2_idmap,
	.dabt_handler		= smmu_dabt_handler,
};
