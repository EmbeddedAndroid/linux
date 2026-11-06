// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keep the host out of the slice of an Arm MMU-500 that the secure firmware
 * programs for remote processor streams.
 *
 * An MMU-500 built without the Security Extensions has no secure stream
 * mapping groups or context banks: the firmware uses the top SMRs, context
 * banks and VMIDs of the non-secure programming interface, and describes
 * that slice in a read-only call to the SPMC. The hypervisor takes the
 * global register pages GR0 and GR1 and the firmware's context bank pages
 * away from the host, and emulates host accesses to GR0 and GR1 so that the
 * host keeps full use of the rest of the SMMU.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/io.h>

#include <asm/kvm_hyp.h>

#include <nvhe/arm-smmu-fw.h>
#include <nvhe/ffa.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

#define SMMU_GR0_sCR0			0x0
#define SMMU_sCR0_VMID16EN		BIT(31)
#define SMMU_sCR0_USFCFG		BIT(10)
#define SMMU_sCR0_EXIDENABLE		BIT(3)
#define SMMU_sCR0_CLIENTPD		BIT(0)
#define SMMU_GR0_ID0			0x20
#define SMMU_ID0_NUMSIDB		GENMASK(12, 9)
#define SMMU_ID0_EXIDS			BIT(8)
#define SMMU_ID0_NUMSMRG		GENMASK(7, 0)
#define SMMU_GR0_ID1			0x24
#define SMMU_ID1_PAGESIZE		BIT(31)
#define SMMU_ID1_NUMPAGENDXB		GENMASK(30, 28)
#define SMMU_ID1_NUMS2CB		GENMASK(23, 16)
#define SMMU_ID1_NUMCB			GENMASK(7, 0)
#define SMMU_GR0_ID2			0x28
#define SMMU_ID2_VMID16			BIT(15)
#define SMMU_GR0_SMR(n)			(0x800 + ((n) << 2))
#define SMMU_SMR_VALID			BIT(31)
#define SMMU_SMR_MASK			GENMASK(30, 16)
#define SMMU_SMR_ID			GENMASK(15, 0)
#define SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define SMMU_S2CR_TYPE			GENMASK(17, 16)
#define SMMU_S2CR_TYPE_BYPASS		1
#define SMMU_S2CR_TYPE_FAULT		2
#define SMMU_S2CR_CBNDX			GENMASK(7, 0)
#define SMMU_GR1_CBAR(n)		((n) << 2)
#define SMMU_CBAR_VMID			GENMASK(7, 0)
#define SMMU_CBAR_S2CBNDX		GENMASK(15, 8)
#define SMMU_CBAR_TYPE			GENMASK(17, 16)
#define SMMU_CBAR_TYPE_S1_S2		3
#define SMMU_GR1_CBFRSYNRA(n)		(0x400 + ((n) << 2))
#define SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define SMMU_MAX_ENTRIES		256

/* OP-TEE blocking call OPTEE_FFA_FW_SMMU_INFO, layout 1 */
#define OPTEE_FFA_FW_SMMU_INFO		9
#define OPTEE_FFA_FW_SMMU_INFO_V1	1
#define TEE_SUCCESS			0
#define TEE_ERROR_ITEM_NOT_FOUND	0xffff0008

#define SMMU_FW_MAX_INSTANCES		2
#define SMMU_FW_MAX_STREAMS		8

struct smmu_fw {
	phys_addr_t		base;
	void __iomem		*gr0;
	void __iomem		*gr1;
	u32			smr_first;
	u32			cb_first;
	u32			vmid_first;
	u32			vmid_count;
	u32			sid_mask;
	u32			nr_streams;
	u32			streams[SMMU_FW_MAX_STREAMS];
	hyp_spinlock_t		lock;
};

static struct smmu_fw smmu_fw[SMMU_FW_MAX_INSTANCES];
static unsigned int nr_smmu_fw;

static bool smr_overlaps(u32 a, u32 b, u32 sid_mask)
{
	u32 ignore = FIELD_GET(SMMU_SMR_MASK, a) | FIELD_GET(SMMU_SMR_MASK, b);

	return !((FIELD_GET(SMMU_SMR_ID, a) ^ FIELD_GET(SMMU_SMR_ID, b)) &
		 ~ignore & sid_mask);
}

static bool smr_is_fw(struct smmu_fw *s, u32 smr)
{
	unsigned int i;

	for (i = 0; i < s->nr_streams; i++)
		if (smr_overlaps(smr, s->streams[i], s->sid_mask))
			return true;
	return false;
}

static bool vmid_is_fw(struct smmu_fw *s, u32 vmid)
{
	return vmid >= s->vmid_first && vmid - s->vmid_first < s->vmid_count;
}

static u32 gr0_read(struct smmu_fw *s, u32 off)
{
	u32 val;

	if (off >= SMMU_GR0_SMR(s->smr_first) &&
	    off < SMMU_GR0_SMR(SMMU_MAX_ENTRIES))
		return 0;
	if (off >= SMMU_GR0_S2CR(s->smr_first) &&
	    off < SMMU_GR0_S2CR(SMMU_MAX_ENTRIES))
		return 0;

	val = readl_relaxed(s->gr0 + off);
	switch (off) {
	case SMMU_GR0_ID0:
		val &= ~SMMU_ID0_NUMSMRG;
		val |= FIELD_PREP(SMMU_ID0_NUMSMRG, s->smr_first);
		break;
	case SMMU_GR0_ID1:
		val = u32_replace_bits(val, s->cb_first, SMMU_ID1_NUMCB);
		val = u32_replace_bits(val, min_t(u32, s->cb_first,
						  FIELD_GET(SMMU_ID1_NUMS2CB, val)),
				       SMMU_ID1_NUMS2CB);
		break;
	case SMMU_GR0_ID2:
		val &= ~SMMU_ID2_VMID16;
		break;
	}
	return val;
}

static void gr0_write(struct smmu_fw *s, u32 off, u32 val)
{
	if (off >= SMMU_GR0_SMR(s->smr_first) &&
	    off < SMMU_GR0_SMR(SMMU_MAX_ENTRIES))
		return;
	if (off >= SMMU_GR0_S2CR(s->smr_first) &&
	    off < SMMU_GR0_S2CR(SMMU_MAX_ENTRIES))
		return;

	if (off == SMMU_GR0_sCR0) {
		/* No global bypass, and only 8-bit VMIDs in CBAR */
		val &= ~(SMMU_sCR0_CLIENTPD | SMMU_sCR0_VMID16EN |
			 SMMU_sCR0_EXIDENABLE);
		val |= SMMU_sCR0_USFCFG;
	} else if (off >= SMMU_GR0_SMR(0) && off < SMMU_GR0_SMR(s->smr_first)) {
		if ((val & SMMU_SMR_VALID) && smr_is_fw(s, val))
			val &= ~SMMU_SMR_VALID;
	} else if (off >= SMMU_GR0_S2CR(0) &&
		   off < SMMU_GR0_S2CR(s->smr_first)) {
		u32 type = FIELD_GET(SMMU_S2CR_TYPE, val);

		if (type != SMMU_S2CR_TYPE_BYPASS &&
		    type != SMMU_S2CR_TYPE_FAULT &&
		    FIELD_GET(SMMU_S2CR_CBNDX, val) >= s->cb_first)
			val = FIELD_PREP(SMMU_S2CR_TYPE, SMMU_S2CR_TYPE_FAULT);
	}

	writel_relaxed(val, s->gr0 + off);
}

static bool gr1_is_fw(struct smmu_fw *s, u32 off)
{
	u32 n = (off & (SZ_1K - 1)) >> 2;

	return off < SMMU_GR1_CBA2R(SMMU_MAX_ENTRIES) && n >= s->cb_first;
}

static u32 gr1_read(struct smmu_fw *s, u32 off)
{
	if (gr1_is_fw(s, off))
		return 0;
	return readl_relaxed(s->gr1 + off);
}

static void gr1_write(struct smmu_fw *s, u32 off, u32 val)
{
	if (gr1_is_fw(s, off))
		return;
	/* A host bank must not share a firmware VMID or nest on its bank */
	if (off < SMMU_GR1_CBFRSYNRA(0) &&
	    (vmid_is_fw(s, FIELD_GET(SMMU_CBAR_VMID, val)) ||
	     (FIELD_GET(SMMU_CBAR_TYPE, val) == SMMU_CBAR_TYPE_S1_S2 &&
	      FIELD_GET(SMMU_CBAR_S2CBNDX, val) >= s->cb_first)))
		return;
	writel_relaxed(val, s->gr1 + off);
}

bool pkvm_arm_smmu_fw_dabt_handler(struct user_pt_regs *regs, u64 esr,
				   u64 addr)
{
	bool write = esr & ESR_ELx_WNR;
	u32 rt = FIELD_GET(ESR_ELx_SRT_MASK, esr);
	struct smmu_fw *s = NULL;
	unsigned int i;
	u64 off, val;

	for (i = 0; i < nr_smmu_fw; i++) {
		if (addr - smmu_fw[i].base < 2 * PAGE_SIZE) {
			s = &smmu_fw[i];
			break;
		}
	}
	if (!s)
		return false;

	/* The driver only makes 32-bit accesses to the global registers */
	off = addr - s->base;
	if (FIELD_GET(ESR_ELx_SAS, esr) != 2 || (off & 3))
		return false;

	hyp_spin_lock(&s->lock);
	if (write) {
		val = rt == 31 ? 0 : regs->regs[rt];
		if (off < PAGE_SIZE)
			gr0_write(s, off, val);
		else
			gr1_write(s, off - PAGE_SIZE, val);
	} else {
		if (off < PAGE_SIZE)
			val = gr0_read(s, off);
		else
			val = gr1_read(s, off - PAGE_SIZE);
		if (esr & ESR_ELx_SSE)
			val = (u64)(s64)(s32)val;
		if (!(esr & ESR_ELx_SF))
			val = lower_32_bits(val);
		if (rt != 31)
			regs->regs[rt] = val;
	}
	hyp_spin_unlock(&s->lock);

	return true;
}

static int fw_smmu_info(u32 smmu, u32 entry, u32 out[4])
{
	struct arm_smccc_1_2_regs res;
	int ret;

	ret = hyp_ffa_spmc_direct_req(&(struct arm_smccc_1_2_regs) {
		.a3 = OPTEE_FFA_FW_SMMU_INFO,
		.a4 = OPTEE_FFA_FW_SMMU_INFO_V1,
		.a5 = smmu,
		.a6 = entry,
	}, &res);
	if (ret)
		return ret;

	switch ((u32)res.a3) {
	case TEE_SUCCESS:
		break;
	case TEE_ERROR_ITEM_NOT_FOUND:
		return -ENOENT;
	default:
		return -EOPNOTSUPP;
	}

	out[0] = res.a4;
	out[1] = res.a5;
	out[2] = res.a6;
	out[3] = res.a7;
	return 0;
}

static int donate_mmio(phys_addr_t pa, u64 nr_pages, void __iomem **va)
{
	unsigned long haddr;
	int ret;

	ret = __pkvm_host_donate_hyp_mmio(hyp_phys_to_pfn(pa), nr_pages,
					  &haddr);
	if (!ret && va)
		*va = (void __iomem *)haddr;
	return ret;
}

static int smmu_fw_probe(u32 idx, struct smmu_fw *s)
{
	u32 nr_smr, nr_cb, numpage, smr_count, cb_count, nr_streams, sidb;
	u32 id0, id1, out[4];
	phys_addr_t cb_base;
	unsigned int i;
	int ret;

	ret = fw_smmu_info(idx, 0, out);
	if (ret)
		return ret;
	s->base = out[0] | (u64)out[1] << 32;
	s->smr_first = out[2] & GENMASK(15, 0);
	smr_count = out[2] >> 16;
	s->cb_first = out[3] & GENMASK(15, 0);
	cb_count = out[3] >> 16;

	ret = fw_smmu_info(idx, 1, out);
	if (ret)
		return ret == -ENOENT ? -EINVAL : ret;
	s->vmid_first = out[0] & GENMASK(15, 0);
	s->vmid_count = out[0] >> 16;
	nr_streams = out[1];
	if (!nr_streams || nr_streams > SMMU_FW_MAX_STREAMS ||
	    !PAGE_ALIGNED(s->base))
		return -EINVAL;

	for (i = 0; i < nr_streams; i++) {
		ret = fw_smmu_info(idx, 2 + i, out);
		if (ret)
			return ret == -ENOENT ? -EINVAL : ret;
		if (out[1] < s->cb_first || out[1] - s->cb_first >= cb_count)
			return -EINVAL;
		s->streams[i] = out[0];
	}
	s->nr_streams = nr_streams;

	ret = donate_mmio(s->base, 2, &s->gr0);
	if (ret)
		return ret;
	s->gr1 = s->gr0 + PAGE_SIZE;

	id0 = readl_relaxed(s->gr0 + SMMU_GR0_ID0);
	id1 = readl_relaxed(s->gr0 + SMMU_GR0_ID1);
	nr_smr = FIELD_GET(SMMU_ID0_NUMSMRG, id0);
	nr_cb = FIELD_GET(SMMU_ID1_NUMCB, id1);
	numpage = 1 << (FIELD_GET(SMMU_ID1_NUMPAGENDXB, id1) + 1);
	sidb = FIELD_GET(SMMU_ID0_NUMSIDB, id0);
	s->sid_mask = sidb ? GENMASK(sidb - 1, 0) : 0;

	/*
	 * The emulation covers 4KiB register pages, 8-bit VMIDs and stream
	 * IDs without the extension, and the firmware slice must be the top
	 * of each range.
	 */
	if ((id1 & SMMU_ID1_PAGESIZE) || PAGE_SIZE != SZ_4K ||
	    (id0 & SMMU_ID0_EXIDS) || !sidb ||
	    s->smr_first + smr_count != nr_smr || !s->smr_first ||
	    s->cb_first + cb_count != nr_cb || !s->cb_first ||
	    s->vmid_first + s->vmid_count > 256)
		return -EINVAL;

	cb_base = s->base + (phys_addr_t)(numpage + s->cb_first) * PAGE_SIZE;
	ret = donate_mmio(cb_base, cb_count, NULL);
	if (ret)
		return ret;

	hyp_spin_lock_init(&s->lock);
	return 0;
}

/*
 * Ask the SPMC for the SMMU slices the secure firmware owns and take them
 * away from the host. No answer leaves every SMMU to the host. Once the
 * firmware claims a slice, failing to fence it fails pKVM initialisation.
 */
int pkvm_arm_smmu_fw_init(void)
{
	u32 out[4];
	int ret;

	if (fw_smmu_info(0, 0, out))
		return 0;

	for (; nr_smmu_fw < SMMU_FW_MAX_INSTANCES; nr_smmu_fw++) {
		ret = smmu_fw_probe(nr_smmu_fw, &smmu_fw[nr_smmu_fw]);
		if (ret == -ENOENT && nr_smmu_fw)
			return 0;
		if (ret)
			return ret;
	}

	/* A slice past the ones this build can fence stays unfenced */
	ret = fw_smmu_info(nr_smmu_fw, 0, out);
	return ret == -ENOENT ? 0 : -E2BIG;
}
