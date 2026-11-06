// SPDX-License-Identifier: GPL-2.0-only
/*
 * PROTOTYPE (P2.3, never merge): attach the firmware streams the host
 * handed over in pkvm_smmu_proto to one stage-2 only context bank of an
 * Arm MMU-500, with an identity map of the 36-bit IPA space, at pKVM init.
 * Functional only: the host keeps access to the SMMU registers.
 */

#include <linux/bitfield.h>
#include <asm/io.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/kvm_pkvm.h>

#include <nvhe/memory.h>
#include <nvhe/mm.h>

struct pkvm_smmu_proto pkvm_smmu_proto;

#define SMMU_GR0_IDR1			0x24
#define  IDR1_PAGESIZE			BIT(31)
#define  IDR1_NUMPAGENDXB		GENMASK(30, 28)
#define  IDR1_NUMCB			GENMASK(7, 0)
#define SMMU_GR0_TLBIVMID		0x64
#define SMMU_GR0_TLBGSYNC		0x70
#define SMMU_GR0_TLBGSTATUS		0x74
#define  TLBGSTATUS_GSACTIVE		BIT(0)
#define SMMU_GR0_SMR(n)			(0x800 + ((n) << 2))
#define  SMR_VALID			BIT(31)
#define SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define SMMU_GR1_CBAR(n)		((n) << 2)
#define SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define  CBA2R_VA64			BIT(0)
#define SMMU_CB_SCTLR			0x0
#define  SCTLR_M			BIT(0)
#define  SCTLR_TRE			BIT(1)
#define  SCTLR_AFE			BIT(2)
#define  SCTLR_CFRE			BIT(5)
#define SMMU_CB_TTBR0			0x20
#define SMMU_CB_TCR			0x30
#define SMMU_CB_FSR			0x58

#define PROTO_VMID			0xf0

/* VTCR format: 36-bit IPA and PA, 4 KiB granule, walk from level 1 */
#define PROTO_VTCR	(BIT(31) | FIELD_PREP(GENMASK(18, 16), 1) |	\
			 FIELD_PREP(GENMASK(13, 12), 3) |		\
			 FIELD_PREP(GENMASK(11, 10), 1) |		\
			 FIELD_PREP(GENMASK(9, 8), 1) |			\
			 FIELD_PREP(GENMASK(7, 6), 1) | 28)

/*
 * Stage-2 1 GiB block, AF, S2AP read/write. MemAttr is the non-FWB
 * encoding the SMMU uses (not the CPU stage-2 one): 0xf Normal WB,
 * 0x1 Device-nGnRE.
 */
#define S2_BLOCK(memattr, sh)	(BIT(10) | ((u64)(sh) << 8) | (3ULL << 6) | \
				 ((u64)(memattr) << 2) | 1)
#define PROTO_NR_BLOCKS		64
#define PROTO_DEVICE_BLOCKS	2

static u64 proto_s2_l1[PAGE_SIZE / sizeof(u64)] __aligned(PAGE_SIZE);

int pkvm_smmu_proto_init(void)
{
	struct pkvm_smmu_proto *p = &pkvm_smmu_proto;
	void __iomem *g0, *g1, *cb;
	unsigned long va;
	u32 idr1, numpage;
	int i, ret, timeout;

	if (!p->base || !p->nr || p->nr > PKVM_SMMU_PROTO_MAX_SIDS)
		return 0;

	ret = __pkvm_create_private_mapping(p->base, 2 * PAGE_SIZE,
					    PAGE_HYP_DEVICE, &va);
	if (ret)
		return ret;
	g0 = (void __iomem *)va;
	g1 = g0 + PAGE_SIZE;

	idr1 = readl_relaxed(g0 + SMMU_GR0_IDR1);
	if ((idr1 & IDR1_PAGESIZE) || PAGE_SIZE != SZ_4K ||
	    PKVM_SMMU_PROTO_CB >= FIELD_GET(IDR1_NUMCB, idr1))
		return -EINVAL;
	numpage = 1 << (FIELD_GET(IDR1_NUMPAGENDXB, idr1) + 1);

	ret = __pkvm_create_private_mapping(p->base +
					    (numpage + PKVM_SMMU_PROTO_CB) * SZ_4K,
					    PAGE_SIZE, PAGE_HYP_DEVICE, &va);
	if (ret)
		return ret;
	cb = (void __iomem *)va;

	for (i = 0; i < PROTO_NR_BLOCKS; i++)
		proto_s2_l1[i] = ((u64)i << 30) |
				 (i < PROTO_DEVICE_BLOCKS ? S2_BLOCK(0x1, 0) :
							    S2_BLOCK(0xf, 3));
	kvm_flush_dcache_to_poc(proto_s2_l1, sizeof(proto_s2_l1));

	writel_relaxed(0, cb + SMMU_CB_SCTLR);
	writel_relaxed(CBA2R_VA64, g1 + SMMU_GR1_CBA2R(PKVM_SMMU_PROTO_CB));
	/* CBAR.TYPE 0: stage 2 only */
	writel_relaxed(PROTO_VMID, g1 + SMMU_GR1_CBAR(PKVM_SMMU_PROTO_CB));
	writeq_relaxed(hyp_virt_to_phys(proto_s2_l1), cb + SMMU_CB_TTBR0);
	writel_relaxed(PROTO_VTCR, cb + SMMU_CB_TCR);
	writel_relaxed(~0U, cb + SMMU_CB_FSR);
	wmb();
	writel_relaxed(SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M,
		       cb + SMMU_CB_SCTLR);

	/* S2CR.TYPE 0: translate in CBNDX */
	for (i = 0; i < p->nr; i++) {
		writel_relaxed(PKVM_SMMU_PROTO_CB,
			       g0 + SMMU_GR0_S2CR(PKVM_SMMU_PROTO_SMR0 + i));
		writel_relaxed(SMR_VALID | p->smr[i],
			       g0 + SMMU_GR0_SMR(PKVM_SMMU_PROTO_SMR0 + i));
	}

	writel_relaxed(PROTO_VMID, g0 + SMMU_GR0_TLBIVMID);
	writel(0, g0 + SMMU_GR0_TLBGSYNC);
	for (timeout = 1000000; timeout; timeout--)
		if (!(readl_relaxed(g0 + SMMU_GR0_TLBGSTATUS) &
		      TLBGSTATUS_GSACTIVE))
			break;

	return timeout ? 0 : -ETIMEDOUT;
}
