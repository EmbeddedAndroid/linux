// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,seca-nwgcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_UFS_0_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_0_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_0_PHY_TX_SYMBOL_0_CLK,
	DT_UFS_1_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_1_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_1_PHY_TX_SYMBOL_0_CLK,
};

enum {
	P_BI_TCXO,
	P_NW_GCC_GPLL0_OUT_MAIN,
	P_NW_GCC_GPLL3_OUT_MAIN,
};

static struct clk_alpha_pll nw_gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x76010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_nw_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv nw_gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_nw_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_nw_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&nw_gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll nw_gcc_gpll3 = {
	.offset = 0x3000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x76010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct parent_map nw_gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data nw_gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map nw_gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_NW_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_NW_GCC_GPLL3_OUT_MAIN, 3 },
};

static const struct clk_parent_data nw_gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &nw_gcc_gpll0.clkr.hw },
	{ .hw = &nw_gcc_gpll3.clkr.hw },
};

static const struct parent_map nw_gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_NW_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_NW_GCC_GPLL3_OUT_MAIN, 5 },
};

static const struct clk_parent_data nw_gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &nw_gcc_gpll0.clkr.hw },
	{ .hw = &nw_gcc_gpll3.clkr.hw },
};

static struct clk_regmap_phy_mux nw_gcc_ufs_0_phy_rx_symbol_0_clk_src = {
	.reg = 0x52068,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_rx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_0_PHY_RX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux nw_gcc_ufs_0_phy_rx_symbol_1_clk_src = {
	.reg = 0x520f0,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_rx_symbol_1_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_0_PHY_RX_SYMBOL_1_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux nw_gcc_ufs_0_phy_tx_symbol_0_clk_src = {
	.reg = 0x52058,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_tx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_0_PHY_TX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux nw_gcc_ufs_1_phy_rx_symbol_0_clk_src = {
	.reg = 0x3d068,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_rx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_1_PHY_RX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux nw_gcc_ufs_1_phy_rx_symbol_1_clk_src = {
	.reg = 0x3d0f0,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_rx_symbol_1_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_1_PHY_RX_SYMBOL_1_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux nw_gcc_ufs_1_phy_tx_symbol_0_clk_src = {
	.reg = 0x3d058,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_tx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_UFS_1_PHY_TX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static const struct freq_tbl ftbl_nw_gcc_ufs_phy_axi_clk_src[] = {
	F(120000000, P_NW_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(201500000, P_NW_GCC_GPLL3_OUT_MAIN, 4, 0, 0),
	F(300000000, P_NW_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	F(403000000, P_NW_GCC_GPLL3_OUT_MAIN, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_nw_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nw_gcc_ufs_0_phy_axi_clk_src = {
	.cmd_rcgr = 0x52034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_1,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_0_phy_axi_clk_src",
		.parent_data = nw_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_0_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x5208c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_1,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_0_phy_ice_core_clk_src",
		.parent_data = nw_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_0_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x520c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_0,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_0_phy_phy_aux_clk_src",
		.parent_data = nw_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_0_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x520a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_2,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_0_phy_unipro_core_clk_src",
		.parent_data = nw_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_1_phy_axi_clk_src = {
	.cmd_rcgr = 0x3d034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_1,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_1_phy_axi_clk_src",
		.parent_data = nw_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_1_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x3d08c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_1,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_1_phy_ice_core_clk_src",
		.parent_data = nw_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_ufs_1_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x3d0c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_0,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_1_phy_phy_aux_clk_src",
		.parent_data = nw_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

/* UFS_1 UniPro takes GPLL3 on mux input 3, not 5 as UFS_0 does */
static struct clk_rcg2 nw_gcc_ufs_1_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x3d0a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_1,
	.freq_tbl = ftbl_nw_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_ufs_1_phy_unipro_core_clk_src",
		.parent_data = nw_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div nw_gcc_gpu_pll0_main_div_clk_src = {
	.reg = 0x23010,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_gpu_pll0_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&nw_gcc_gpll0_out_even.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch nw_gcc_aggre_noc_ufs_0_phy_axi_clk = {
	.halt_reg = 0x520f8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x520f8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x520f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_aggre_noc_ufs_0_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_aggre_noc_ufs_1_phy_axi_clk = {
	.halt_reg = 0x3d0f8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d0f8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d0f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_aggre_noc_ufs_1_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_gpll0_clk_src = {
	.halt_reg = 0x23018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_gpll0_div_clk_src = {
	.halt_reg = 0x23020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpu_pll0_main_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_smmu_vote_clk = {
	.halt_reg = 0x86038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x86038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_smmu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_ahb_clk = {
	.halt_reg = 0x52028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_axi_clk = {
	.halt_reg = 0x52018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x52018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_ice_core_clk = {
	.halt_reg = 0x5207c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5207c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5207c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_phy_aux_clk = {
	.halt_reg = 0x520bc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x520bc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x520bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_rx_symbol_0_clk = {
	.halt_reg = 0x52030,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_rx_symbol_1_clk = {
	.halt_reg = 0x520d8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x520d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_tx_symbol_0_clk = {
	.halt_reg = 0x5202c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x5202c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_0_phy_unipro_core_clk = {
	.halt_reg = 0x5206c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5206c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5206c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_0_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_0_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_ahb_clk = {
	.halt_reg = 0x3d028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_axi_clk = {
	.halt_reg = 0x3d018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_ice_core_clk = {
	.halt_reg = 0x3d07c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d07c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_phy_aux_clk = {
	.halt_reg = 0x3d0bc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d0bc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d0bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_rx_symbol_0_clk = {
	.halt_reg = 0x3d030,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3d030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_rx_symbol_1_clk = {
	.halt_reg = 0x3d0d8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3d0d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_tx_symbol_0_clk = {
	.halt_reg = 0x3d02c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3d02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_ufs_1_phy_unipro_core_clk = {
	.halt_reg = 0x3d06c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3d06c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3d06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_ufs_1_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_ufs_1_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_video_axi0_clk = {
	.halt_reg = 0x1a008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_video_axi0c_clk = {
	.halt_reg = 0x1a01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_video_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc nw_gcc_ufs_0_mem_phy_gdsc = {
	.gdscr = 0x3b000,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "nw_gcc_ufs_0_mem_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc nw_gcc_ufs_0_phy_gdsc = {
	.gdscr = 0x52004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "nw_gcc_ufs_0_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc nw_gcc_ufs_1_mem_phy_gdsc = {
	.gdscr = 0x3c000,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "nw_gcc_ufs_1_mem_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc nw_gcc_ufs_1_phy_gdsc = {
	.gdscr = 0x3d004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "nw_gcc_ufs_1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *nw_gcc_seca_clocks[] = {
	[NW_GCC_AGGRE_NOC_UFS_0_PHY_AXI_CLK] = &nw_gcc_aggre_noc_ufs_0_phy_axi_clk.clkr,
	[NW_GCC_AGGRE_NOC_UFS_1_PHY_AXI_CLK] = &nw_gcc_aggre_noc_ufs_1_phy_axi_clk.clkr,
	[NW_GCC_GPLL0] = &nw_gcc_gpll0.clkr,
	[NW_GCC_GPLL3] = &nw_gcc_gpll3.clkr,
	[NW_GCC_UFS_0_PHY_AHB_CLK] = &nw_gcc_ufs_0_phy_ahb_clk.clkr,
	[NW_GCC_UFS_0_PHY_AXI_CLK] = &nw_gcc_ufs_0_phy_axi_clk.clkr,
	[NW_GCC_UFS_0_PHY_AXI_CLK_SRC] = &nw_gcc_ufs_0_phy_axi_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_ICE_CORE_CLK] = &nw_gcc_ufs_0_phy_ice_core_clk.clkr,
	[NW_GCC_UFS_0_PHY_ICE_CORE_CLK_SRC] = &nw_gcc_ufs_0_phy_ice_core_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_PHY_AUX_CLK] = &nw_gcc_ufs_0_phy_phy_aux_clk.clkr,
	[NW_GCC_UFS_0_PHY_PHY_AUX_CLK_SRC] = &nw_gcc_ufs_0_phy_phy_aux_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_RX_SYMBOL_0_CLK] = &nw_gcc_ufs_0_phy_rx_symbol_0_clk.clkr,
	[NW_GCC_UFS_0_PHY_RX_SYMBOL_0_CLK_SRC] = &nw_gcc_ufs_0_phy_rx_symbol_0_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_RX_SYMBOL_1_CLK] = &nw_gcc_ufs_0_phy_rx_symbol_1_clk.clkr,
	[NW_GCC_UFS_0_PHY_RX_SYMBOL_1_CLK_SRC] = &nw_gcc_ufs_0_phy_rx_symbol_1_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_TX_SYMBOL_0_CLK] = &nw_gcc_ufs_0_phy_tx_symbol_0_clk.clkr,
	[NW_GCC_UFS_0_PHY_TX_SYMBOL_0_CLK_SRC] = &nw_gcc_ufs_0_phy_tx_symbol_0_clk_src.clkr,
	[NW_GCC_UFS_0_PHY_UNIPRO_CORE_CLK] = &nw_gcc_ufs_0_phy_unipro_core_clk.clkr,
	[NW_GCC_UFS_0_PHY_UNIPRO_CORE_CLK_SRC] = &nw_gcc_ufs_0_phy_unipro_core_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_AHB_CLK] = &nw_gcc_ufs_1_phy_ahb_clk.clkr,
	[NW_GCC_UFS_1_PHY_AXI_CLK] = &nw_gcc_ufs_1_phy_axi_clk.clkr,
	[NW_GCC_UFS_1_PHY_AXI_CLK_SRC] = &nw_gcc_ufs_1_phy_axi_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_ICE_CORE_CLK] = &nw_gcc_ufs_1_phy_ice_core_clk.clkr,
	[NW_GCC_UFS_1_PHY_ICE_CORE_CLK_SRC] = &nw_gcc_ufs_1_phy_ice_core_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_PHY_AUX_CLK] = &nw_gcc_ufs_1_phy_phy_aux_clk.clkr,
	[NW_GCC_UFS_1_PHY_PHY_AUX_CLK_SRC] = &nw_gcc_ufs_1_phy_phy_aux_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_RX_SYMBOL_0_CLK] = &nw_gcc_ufs_1_phy_rx_symbol_0_clk.clkr,
	[NW_GCC_UFS_1_PHY_RX_SYMBOL_0_CLK_SRC] = &nw_gcc_ufs_1_phy_rx_symbol_0_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_RX_SYMBOL_1_CLK] = &nw_gcc_ufs_1_phy_rx_symbol_1_clk.clkr,
	[NW_GCC_UFS_1_PHY_RX_SYMBOL_1_CLK_SRC] = &nw_gcc_ufs_1_phy_rx_symbol_1_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_TX_SYMBOL_0_CLK] = &nw_gcc_ufs_1_phy_tx_symbol_0_clk.clkr,
	[NW_GCC_UFS_1_PHY_TX_SYMBOL_0_CLK_SRC] = &nw_gcc_ufs_1_phy_tx_symbol_0_clk_src.clkr,
	[NW_GCC_UFS_1_PHY_UNIPRO_CORE_CLK] = &nw_gcc_ufs_1_phy_unipro_core_clk.clkr,
	[NW_GCC_UFS_1_PHY_UNIPRO_CORE_CLK_SRC] = &nw_gcc_ufs_1_phy_unipro_core_clk_src.clkr,
	[NW_GCC_GPLL0_OUT_EVEN] = &nw_gcc_gpll0_out_even.clkr,
	[NW_GCC_GPU_GPLL0_CLK_SRC] = &nw_gcc_gpu_gpll0_clk_src.clkr,
	[NW_GCC_GPU_GPLL0_DIV_CLK_SRC] = &nw_gcc_gpu_gpll0_div_clk_src.clkr,
	[NW_GCC_GPU_PLL0_MAIN_DIV_CLK_SRC] = &nw_gcc_gpu_pll0_main_div_clk_src.clkr,
	[NW_GCC_GPU_SMMU_VOTE_CLK] = &nw_gcc_gpu_smmu_vote_clk.clkr,
	[NW_GCC_VIDEO_AXI0_CLK] = &nw_gcc_video_axi0_clk.clkr,
	[NW_GCC_VIDEO_AXI0C_CLK] = &nw_gcc_video_axi0c_clk.clkr,
};

static struct gdsc *nw_gcc_seca_gdscs[] = {
	[NW_GCC_UFS_0_MEM_PHY_GDSC] = &nw_gcc_ufs_0_mem_phy_gdsc,
	[NW_GCC_UFS_0_PHY_GDSC] = &nw_gcc_ufs_0_phy_gdsc,
	[NW_GCC_UFS_1_MEM_PHY_GDSC] = &nw_gcc_ufs_1_mem_phy_gdsc,
	[NW_GCC_UFS_1_PHY_GDSC] = &nw_gcc_ufs_1_phy_gdsc,
};

static const struct qcom_reset_map nw_gcc_seca_resets[] = {
	[NW_GCC_UFS_0_PHY_BCR] = { 0x52000 },
	[NW_GCC_UFS_1_PHY_BCR] = { 0x3d000 },
	[NW_GCC_VIDEO_BCR] = { 0x1a000 },
	[NW_GCC_VIDEO_AXI0_CLK_ARES] = { 0x1a008, 2 },
	[NW_GCC_VIDEO_AXI0C_CLK_ARES] = { 0x1a01c, 2 },
};

static const struct regmap_config nw_gcc_seca_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf41f0,
	.fast_io = true,
};

static void clk_seca_regs_configure(struct device *dev, struct regmap *regmap)
{
	/* FORCE_MEM_CORE_ON for the UFS ICE and AXI branches, as on Nord */
	qcom_branch_set_force_mem_core(regmap, nw_gcc_ufs_0_phy_ice_core_clk, true);
	qcom_branch_set_force_mem_core(regmap, nw_gcc_ufs_0_phy_axi_clk, true);
	qcom_branch_set_force_mem_core(regmap, nw_gcc_ufs_1_phy_ice_core_clk, true);
	qcom_branch_set_force_mem_core(regmap, nw_gcc_ufs_1_phy_axi_clk, true);
}

static const u32 nw_gcc_seca_critical_cbcrs[] = {
	0x23004, /* NW_GCC_GPU_CFG_AHB_CLK */
	0x1a004, /* NW_GCC_VIDEO_AHB_CLK */
	0x1a030, /* NW_GCC_VIDEO_XO_CLK */
};

static const struct qcom_cc_driver_data nw_gcc_seca_driver_data = {
	.clk_cbcrs = nw_gcc_seca_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(nw_gcc_seca_critical_cbcrs),
	.clk_regs_configure = clk_seca_regs_configure,
};

static const struct qcom_cc_desc nw_gcc_seca_desc = {
	.config = &nw_gcc_seca_regmap_config,
	.clks = nw_gcc_seca_clocks,
	.num_clks = ARRAY_SIZE(nw_gcc_seca_clocks),
	.resets = nw_gcc_seca_resets,
	.num_resets = ARRAY_SIZE(nw_gcc_seca_resets),
	.gdscs = nw_gcc_seca_gdscs,
	.num_gdscs = ARRAY_SIZE(nw_gcc_seca_gdscs),
	.driver_data = &nw_gcc_seca_driver_data,
};

static const struct of_device_id nw_gcc_seca_match_table[] = {
	{ .compatible = "qcom,seca-nwgcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, nw_gcc_seca_match_table);

static int nw_gcc_seca_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &nw_gcc_seca_desc);
}

static struct platform_driver nw_gcc_seca_driver = {
	.probe = nw_gcc_seca_probe,
	.driver = {
		.name = "nwgcc-seca",
		.of_match_table = nw_gcc_seca_match_table,
	},
};

module_platform_driver(nw_gcc_seca_driver);

MODULE_DESCRIPTION("QTI NWGCC SecaAU Driver");
MODULE_LICENSE("GPL");
